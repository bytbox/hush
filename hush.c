#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#ifndef DEFAULT_PORT
#define DEFAULT_PORT "63212"
#endif

#define EXIT(x) eval = x; goto _exit

// TODO
// #include <alsa/asoundlib.h>
// http://www.suse.de/~mana/alsa090_howto.html

char *pname;

int client(const char *, const char *);
int server(const char *);

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
	}
	if (connect(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		fprintf(stderr, "%s: connect: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}

	FILE *sock = fdopen(sockfd, "rw");
	fclose(sock);

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
	if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		fprintf(stderr, "%s: bind: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}
	if (listen(sockfd, 0)) {
		fprintf(stderr, "%s: listen: %s\n", pname, strerror(errno));
		EXIT(EXIT_FAILURE);
	}

	FILE *sock = fdopen(sockfd, "rw");
	fclose(sock);

_exit:
	if (ai) freeaddrinfo(ai);
	return eval;
}

