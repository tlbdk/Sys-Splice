/*
 * A tee implementation using sys_tee. Sends out the data received over
 * stdin to the given host:port and over stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>

#include "splice.h"

static int do_splice(int infd, int outfd, unsigned int len, char *msg)
{
	while (len) {
		int written = ssplice(infd, NULL, outfd, NULL, len, 0);

		if (written <= 0)
			return error(msg);

		len -= written;
	}

	return 0;
}

static int usage(char *name)
{
	fprintf(stderr, "... | %s: hostname:port\n", name);
	return 1;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	char *p, *hname;
	int fd;

	if (argc < 2)
		return usage(argv[0]);

	if (check_input_pipe())
		return usage(argv[0]);

	hname = strdup(argv[1]);
	p = strstr(hname, ":");
	if (!p)
		return usage(argv[0]);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(p + 1));
	*p = '\0';

	if (inet_aton(hname, &addr.sin_addr) != 1) {
		struct hostent *hent = gethostbyname(hname);

		if (!hent)
			return error("gethostbyname");

		memcpy(&addr.sin_addr, hent->h_addr, 4);
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return error("socket");

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
		return error("connect");

	do {
		int tee_len = stee(STDIN_FILENO, STDOUT_FILENO, INT_MAX, 0);

		if (tee_len < 0) {
			if (errno == EAGAIN) {
				usleep(1000);
				continue;
			}
			return error("tee");
		} else if (!tee_len)
			break;

		/*
		 * Send output to file, also consumes input pipe.
		 */
		if (do_splice(STDIN_FILENO, fd, tee_len, "splice-net"))
			break;
	} while (1);

	close(fd);
	return 0;
}
