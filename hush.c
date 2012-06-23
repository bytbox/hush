// Standard Libraries
#include <errno.h>
#include <signal.h> // TODO
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Networking
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

// Audio
// #include <alsa/asoundlib.h>
// http://www.suse.de/~mana/alsa090_howto.html

#ifndef DEFAULT_PORT
#define DEFAULT_PORT "63212"
#endif

#define EXIT(x) eval = x; goto _exit

#define COPY1(d, s) memcpy(&(d), (s), sizeof(d))
#define COPY2(d, s) memcpy((d), &(s), sizeof(s))

enum TYPE {TEXT, SOUND};

// Flags for data

char *pname;

int client(const char *, const char *);
int server(const char *);

int chat(int);

int main(int argc, char *argv[]) {
	pname = argv[0];
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s -l[isten] [port]\n", pname);
		fprintf(stderr, "       %s host [port]\n", pname);
		return EXIT_FAILURE;
	}

	char *host = NULL, *port = DEFAULT_PORT;
	int eval = EXIT_SUCCESS;

	// Get the host (or decide to listen)
	if (strncmp("-l", argv[1], 2))
		host = strdup(argv[1]);
	if (host && host[0] == '-') {
		fprintf(stderr, "%s: %s: unrecognized flag\n", pname, argv[1]);
		eval = EXIT_FAILURE;
		goto _exit;
	}

	// Get the port
	if (argc == 3) port = argv[2];

	if (!host) eval = server(port);
	else eval = client(host, port);

_exit:
	if (host) free(host);
	return eval;
}

int client(const char *host, const char *port) {
	struct addrinfo *ai = NULL;
	int en = 0, eval = EXIT_SUCCESS;
	if ((en = getaddrinfo(host, port, NULL, &ai))) {
		fprintf(stderr, "%s: getaddrinfo: %s\n", pname, gai_strerror(en));
		EXIT(EXIT_FAILURE);
	}

	int sockfd = socket(ai->ai_family, SOCK_STREAM, 0);
	if (sockfd < 0) {
		fprintf(stderr, "%s: socket: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}
	int ndflag = 1;
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &ndflag, sizeof(ndflag))) {
		fprintf(stderr, "%s: setsockopt: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}
	if (connect(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		fprintf(stderr, "%s: connect: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}

	if (chat(sockfd) < 0)
		EXIT(EXIT_FAILURE);

_exit:
	if (ai) freeaddrinfo(ai);
	return eval;
}

int server(const char *port) {
	struct addrinfo *ai = NULL;
	int en = 0, eval = EXIT_SUCCESS;
	if ((en = getaddrinfo(NULL, port, NULL, &ai))) {
		fprintf(stderr, "%s: getaddrinfo: %s\n", pname, gai_strerror(en));
		EXIT(EXIT_FAILURE);
	}
	
	int sockfd = socket(ai->ai_family, SOCK_STREAM, 0);
	if (sockfd < 0) {
		fprintf(stderr, "%s: socket: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}
	int ndflag = 1;
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &ndflag, sizeof(ndflag))) {
		fprintf(stderr, "%s: setsockopt: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}
	if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		fprintf(stderr, "%s: bind: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}
_listen:
	if (listen(sockfd, 0) < 0) {
		fprintf(stderr, "%s: listen: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}
	int fd = accept(sockfd, NULL, NULL);
	if (fd < 0) {
		fprintf(stderr, "%s: listen: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}

	chat(fd);

	goto _listen;

_exit:
	if (ai) freeaddrinfo(ai);
	return eval;
}

enum OFFSETS {LENGTH=0, TYPE=2, FLAGS=3, DATA=4};

int send_data(int fd, short len, enum TYPE type, char flags, const char *data) {
	/* The format of the data sent is:
	 *   1-2 length
	 *   3   type
	 *   4   flags
	 *   5+  data
	 */
	if (len >= (1 << 16)) { // Send no more than 64 KB of data.
		errno = EOVERFLOW;
		return EXIT_FAILURE;
	}
	int plen = len + DATA;
	char *pbuf = (char *)malloc(plen);
	COPY2(pbuf + LENGTH, len);
	COPY2(pbuf + TYPE, len);
	COPY2(pbuf + FLAGS, len);
	memcpy(pbuf + DATA, data, len);

	if (write(fd, pbuf, plen) < 0) {
		int x = errno;
		free(pbuf);
		errno = x;
		return EXIT_FAILURE;
	}

	free(pbuf);
	return EXIT_SUCCESS;
}

struct packet_info {
	short len;
	enum TYPE type;
	char flags;
};

int read_data(int fd, struct packet_info *pi, char **data, int bufsize) {
	static char tbuf[DATA];
	if (read(fd, tbuf, sizeof(tbuf)) < 0)
		return EXIT_FAILURE;
	COPY1(pi->len, tbuf + LENGTH);
	COPY1(pi->type, tbuf + TYPE);
	COPY1(pi->flags, tbuf + FLAGS);

	if (pi->len > bufsize) {
		if (*data) free(*data);
		while (pi->len > bufsize) bufsize *= 1;
		*data = (char *)malloc(bufsize);
	}
	if (read(fd, *data, pi->len) < 0)
		return EXIT_FAILURE;
	return bufsize;
}

int chat(int nfd) {
	int eval = EXIT_SUCCESS;

	// We need to be reading from the network, as well as any local input
	// streams.
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(fileno(stdin), &fds);
	FD_SET(nfd, &fds);

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 10000; // 10ms
	while (select(2, &fds, NULL, NULL, &tv) >= 0) {
		tv.tv_sec = 0;
		tv.tv_usec = 10000; // 10ms
	}

	//select(2, fds, 0, 0, NULL);

	if (send_data(nfd, 10, TEXT, 0, "hellotheremyfriend") < 0) {
		fprintf(stderr, "%s: send_data: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}

_exit:
	close(nfd);
	return eval;
}

