// Standard Libraries
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Networking
#include <sys/types.h>
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

// Flags for data.
#define SOUND (1 << 1)

char *pname;

int client(const char *, const char *);
int server(const char *);

int send_data(int, char, short, const char *);

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

	if (send_data(sockfd, 0, 5000, "hellot") < 0) {
		fprintf(stderr, "%s: send_data: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}

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

	FILE *sock = fdopen(fd, "rw");
	fclose(sock);

	goto _listen;

_exit:
	if (ai) freeaddrinfo(ai);
	return eval;
}

int send_data(int fd, char flags, short len, const char *data) {
	/* The format of the data sent is:
	 *   1   flags
	 *   2   length (in qwords)
	 *   3-4 other
	 *   5+  data
	 */
	if (len >= (1 << 16)) { // Send no more than 64 KB of data.
		errno = EOVERFLOW;
		return -1;
	}
	return 0;
}

