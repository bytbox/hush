// TODO handle signals more nicely

// Standard Libraries
#include <errno.h>
#include <signal.h>
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

// Types of packets that can be sent.
enum TYPE {TEXT, SOUND};

// Flags for data
// (none, right now)

// Offsets of fields in a data packet.
enum OFFSETS {OLENGTH=0, OTYPE=2, OFLAGS=3, ODATA=4};

// Internal representation of a packet.
struct packet_info {
	short len;
	enum TYPE type;
	char flags;
};

char *pname; // argv[0] - for error reporting

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
	// Simple networking code to connect to the specified host and port.
	// The actual logic is in chat().
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
	// Simple networking code to start a server and handle a series of
	// clients until user termination, ignoring failures. The actual logic
	// is in chat().
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

// Send a data packet over the network. On failure, we return EXIT_FAILURE set
// errno.
int send_data(int fd, short len, enum TYPE typ, char flags, const char *data) {
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
	int plen = len + ODATA;
	char *pbuf = (char *)malloc(plen);
	COPY2(pbuf + OLENGTH, len);
	COPY2(pbuf + OTYPE, typ);
	COPY2(pbuf + OFLAGS, flags);
	memcpy(pbuf + ODATA, data, len);

	if (write(fd, pbuf, plen) < plen) {
		// An insufficient amount of data could be written. Assume an
		// error and bail out.
		int x = errno;
		free(pbuf);
		errno = x;
		return EXIT_FAILURE;
	}

	free(pbuf);
	return EXIT_SUCCESS;
}

// Read a data packet from the network.
// 
// Our return value will be -1 if an error was encountered (such as a corrupted
// packet), 0 if EOF was reached (without error), and otherwise the size of the
// data received. The data and any available meta-information will be placed in
// the passed packet_info struct.
int read_data(int fd, struct packet_info *pi, char **data, int bufsize) {
	static char tbuf[ODATA];
	int c;
	c = read(fd, tbuf, sizeof(tbuf));
	if (c < 0)
		return -1;
	if (c != sizeof(tbuf)) // EOF
		return 0;
	COPY1(pi->len, tbuf + OLENGTH);
	COPY1(pi->type, tbuf + OTYPE);
	COPY1(pi->flags, tbuf + OFLAGS);

	if (pi->len > bufsize) {
		if (bufsize < 1) {
			bufsize = 1;
		} else free(*data);
		while (pi->len > bufsize) bufsize *= 2;
		*data = (char *)malloc(bufsize);
	}
	c = read(fd, *data, pi->len);
	if (c != pi->len)
		return -1;
	return bufsize;
}

#ifndef TEXT_BUF_SIZE
#define TEXT_BUF_SIZE (1 << 10)
#endif

int chat(int nfd) {
	int eval = EXIT_SUCCESS;
	char text_buf[TEXT_BUF_SIZE];

	fprintf(stderr, "  :connection open\n");

	// We use select() to read from input streams, but we must have a very
	// short timeout to allow us to collect (and play) audio.
	fd_set fds;
	int nfds = nfd+1;
	struct timeval tv;

	while (1) {
		// prepare socket list
		FD_ZERO(&fds);
		FD_SET(fileno(stdin), &fds);
		FD_SET(nfd, &fds);

		// set the timeout
		tv.tv_sec = 0;
		tv.tv_usec = 10000; // 10ms

		if (select(nfds, &fds, NULL, NULL, &tv) < 0) {
			fprintf(stderr, "%s: select: %s\n",
					pname, strerror(errno));
			EXIT(EXIT_FAILURE);
		}

		if (FD_ISSET(fileno(stdin), &fds)) { // from stdin
			int c = read(fileno(stdin), text_buf, TEXT_BUF_SIZE);
			if (c < 0) {
				fprintf(stderr, "%s: read(stdin, ...): %s\n",
						pname, strerror(errno));
				EXIT(EXIT_FAILURE);
			}

			if (c == 0) {
				// end of file - this means the user is trying
				// to terminate the connection.
				EXIT(EXIT_SUCCESS);
			}

			// send whatever was read.
			if (send_data(nfd, c, TEXT, 0, text_buf) < 0) {
				fprintf(stderr, "%s: send_data: %s\n",
						pname, strerror(errno));
				EXIT(EXIT_FAILURE);
			}
		}

		if (FD_ISSET(nfd, &fds)) { // from network
			static char *data;
			static int bufsize = 0;
			struct packet_info pi;
			int c = read_data(nfd, &pi, &data, bufsize);
			if (c < 0) {
				fprintf(stderr, "%s: read_data: %s\n",
						pname, strerror(errno));
				EXIT(EXIT_FAILURE);
			}
			if (c == 0) {
				// The remote end terminated the connection.
				fprintf(stderr, "  :connection terminated\n");
				EXIT(EXIT_SUCCESS);
			}
			// data was read succesfully
			switch (pi.type) {
			case TEXT:
				// Just spit it out.
				fwrite(data, 1, c, stdout);
				break;
			case SOUND:
				break;
			}
		}
	}

_exit:
	close(nfd);
	return eval;
}

