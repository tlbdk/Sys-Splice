/*
 * Splice from network to stdout
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
#include <sys/poll.h>

#include "splice.h"

static unsigned int splice_size = SPLICE_SIZE;
static unsigned int splice_flags;
static int wait_for_poll;

static int usage(char *name)
{
	fprintf(stderr, "%s: [-s splice size] [-w wait for poll] [-n non-blocking] port\n", name);
	return 1;
}

static int splice_from_net(int fd)
{
	while (1) {
		int ret;

		if (wait_for_poll) {
			struct pollfd pfd = {
				.fd = fd,
				.events = POLLIN,
			};

			ret = poll(&pfd, 1, -1);
			if (ret < 0)
				return error("poll");
			else if (!ret)
				continue;

			if (!(pfd.revents & POLLIN))
				continue;
		}

		ret = ssplice(fd, NULL, STDOUT_FILENO, NULL, splice_size, 0);

		if (ret < 0)
			return error("splice");
		else if (!ret)
			break;
	}

	return 0;
}

static int get_connect(int fd, struct sockaddr_in *addr)
{
	socklen_t socklen = sizeof(*addr);
	int ret, connfd;

	do {
		struct pollfd pfd = {
			.fd = fd,
			.events = POLLIN,
		};

		ret = poll(&pfd, 1, -1);
		if (ret < 0)
			return error("poll");
		else if (!ret)
			continue;

		connfd = accept(fd, (struct sockaddr *) addr, &socklen);
		if (connfd < 0)
			return error("accept");
		break;
	} while (1);
			
	return connfd;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "s:w:n")) != -1) {
		switch (c) {
		case 's':
			splice_size = atoi(optarg);
			index++;
			break;
		case 'w':
			wait_for_poll = atoi(optarg);
			index++;
			break;
		case 'n':
			splice_flags |= SPLICE_F_NONBLOCK;
			index++;
			break;
		default:
			return -1;
		}
	}

	return index;
}


int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	unsigned short port;
	int connfd, opt, fd, index;

	if (argc < 2)
		return usage(argv[0]);

	if (check_output_pipe())
		return usage(argv[0]);

	index = parse_options(argc, argv);
	if (index == -1 || index + 1 > argc)
		return usage(argv[0]);

	port = atoi(argv[index]);

	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0)
		return error("socket");

	opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		return error("setsockopt");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
		return error("bind");
	if (listen(fd, 1) < 0)
		return error("listen");

	connfd = get_connect(fd, &addr);
	if (connfd < 0)
		return connfd;

	return splice_from_net(connfd);
}
