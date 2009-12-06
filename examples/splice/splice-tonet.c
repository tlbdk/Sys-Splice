/*
 * Splice stdin to net
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#include "splice.h"

static int usage(char *name)
{
	fprintf(stderr, "%s: target port\n", name);
	return 1;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	unsigned short port;
	int fd, ret;

	if (argc < 3)
		return usage(argv[0]);

	if (check_input_pipe())
		return usage(argv[0]);

	port = atoi(argv[2]);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (inet_aton(argv[1], &addr.sin_addr) != 1) {
		struct hostent *hent = gethostbyname(argv[1]);

		if (!hent)
			return error("gethostbyname");

		memcpy(&addr.sin_addr, hent->h_addr, 4);
	}

	printf("Connecting to %s/%d\n", argv[1], port);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return error("socket");

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
		return error("connect");

	do {
		ret = ssplice(STDIN_FILENO, NULL, fd, NULL, SPLICE_SIZE, SPLICE_F_NONBLOCK);
		if (ret < 0) {
			if (errno == EAGAIN) {
				usleep(100);
				continue;
			}
			return error("splice");
		} else if (!ret)
			break;
	} while (1);

	close(fd);
	return 0;
}
