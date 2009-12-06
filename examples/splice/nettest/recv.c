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
#include <sys/time.h>
#include <sys/resource.h>

#include "../splice.h"
#include "crc32.h"
#include "msg.h"

static unsigned int msg_size = 4096;
static int use_splice = 1;
static int splice_move;

static int usage(const char *name)
{
	fprintf(stderr, "%s: [-s(ize)] [-m(ove)] [-r(ecv)] port\n", name);
	return 1;
}

unsigned long mtime_since(struct timeval *s, struct timeval *e)
{
	long sec, usec, ret;

	sec = e->tv_sec - s->tv_sec;
	usec = e->tv_usec - s->tv_usec;
	if (sec > 0 && usec < 0) {
		sec--;
		usec += 1000000;
	}

	sec *= 1000UL;
	usec /= 1000UL;
	ret = sec + usec;

	/*
	 * time warp bug on some kernels?
	 */
	if (ret < 0)
		ret = 0;

	return ret;
}

unsigned long mtime_since_now(struct timeval *s)
{
	struct timeval t;

	gettimeofday(&t, NULL);
	return mtime_since(s, &t);
}

static int get_connect(int fd, struct sockaddr_in *addr)
{
	socklen_t socklen = sizeof(*addr);
	int ret, connfd;

	fprintf(stderr, "Waiting for connect...\n");

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

	fprintf(stderr, "Got connect!\n");
			
	return connfd;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "s:mr")) != -1) {
		switch (c) {
		case 's':
			msg_size = atoi(optarg);
			index++;
			break;
		case 'm':
			splice_move = 1;
			index++;
			break;
		case 'r':
			use_splice = 0;
			index++;
			break;
		default:
			return -1;
		}
	}

	return index;
}

static int verify_crc(struct msg *m)
{
	unsigned long crc;
	void *data = m;

	data += sizeof(*m);
	crc = crc32(data, m->msg_size - sizeof(*m));

	if (crc == m->crc32)
		return 0;

	fprintf(stderr, "crc error: got %lx, wanted %lx\n", crc, m->crc32);
	return 1;
}

static int do_recv(int fd, void *buf, unsigned int len)
{
	while (len) {
		int ret = recv(fd, buf, len, MSG_WAITALL);

		if (ret < 0)
			return error("recv");
		else if (!ret)
			break;

		len -= ret;
		buf += ret;
	}

	return len;
}

static int normal_recv_loop(int fd)
{
	struct msg *m;

	m = malloc(msg_size);

	while (1) {
		if (do_recv(fd, m, msg_size))
			break;

		if (m->msg_size != msg_size) {
			fprintf(stderr, "Bad packet length: wanted %u, got %lu\n", msg_size, m->msg_size);
			break;
		}

		/*
		 * now verify data
		 */
		if (verify_crc(m))
			break;
	}

	free(m);
	return 0;
}

static int splice_in(int sockfd, int pipefd, unsigned int size)
{
	while (size) {
		int ret = ssplice(sockfd, NULL, pipefd, NULL, size, 0);

		if (ret < 0)
			return error("splice from net");
		else if (!ret)
			break;

		size -= ret;
	}

	if (size)
		fprintf(stderr, "splice: %u resid\n", size);

	return size;
}

static int vmsplice_unmap(int pipefd, void *buf, unsigned int len)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};

	if (svmsplice(pipefd, &iov, 1, SPLICE_F_UNMAP) < 0)
		return error("vmsplice unmap");

	return 0;
}

static int vmsplice_out(void **buf, int pipefd, unsigned int len)
{
	struct iovec iov = {
		.iov_base = *buf,
		.iov_len = len,
	};
	int ret, flags = 0;

	if (splice_move)
		flags |= SPLICE_F_MOVE;

	while (len) {
		ret = svmsplice(pipefd, &iov, 1, flags);
		if (ret < 0)
			return error("vmsplice");
		else if (!ret)
			break;

		*buf = iov.iov_base;

		len -= ret;
		if (len) {
			if (splice_move)
				break;
			iov.iov_len -= ret;
			iov.iov_base += ret;
		}
	}

	if (len)
		fprintf(stderr, "vmsplice: %u resid\n", len);

	return len;
}

static int splice_recv_loop(int fd)
{
	struct msg *m;
	void *buf;
	int pipes[2];

	if (pipe(pipes) < 0)
		return error("pipe");

	if (!splice_move)
		m = malloc(msg_size);
	else
		m = NULL;

	while (1) {
		/*
		 * fill pipe with network data
		 */
		if (splice_in(fd, pipes[1], msg_size))
			break;

		/*
		 * move data to our address space
		 */
		if (!splice_move)
			buf = m;
		else
			buf = NULL;

		if (vmsplice_out(&buf, pipes[0], msg_size))
			break;

		m = buf;

		if (m->msg_size != msg_size) {
			fprintf(stderr, "Bad packet length: wanted %u, got %lu\n", msg_size, m->msg_size);
			break;
		}

		/*
		 * now verify data
		 */
		if (verify_crc(m))
			break;

		if (splice_move && vmsplice_unmap(pipes[0], buf, msg_size))
			break;
	}

	if (!splice_move)
		free(m);

	close(pipes[0]);
	close(pipes[1]);
	return 0;
}

static int recv_loop(int fd)
{
	struct rusage ru_s, ru_e;
	struct timeval start;
	unsigned long ut, st, rt;
	int ret;

	gettimeofday(&start, NULL);
	getrusage(RUSAGE_SELF, &ru_s);

	if (use_splice)
		ret = splice_recv_loop(fd);
	else
		ret = normal_recv_loop(fd);

	getrusage(RUSAGE_SELF, &ru_e);

	ut = mtime_since(&ru_s.ru_utime, &ru_e.ru_utime);
	st = mtime_since(&ru_s.ru_stime, &ru_e.ru_stime);
	rt = mtime_since_now(&start);

	printf("usr=%lu, sys=%lu, real=%lu\n", ut, st, rt);

	return ret;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	unsigned short port;
	int connfd, fd, opt, index;

	if (argc < 2)
		return usage(argv[0]);

	index = parse_options(argc, argv);
	if (index == -1 || index + 1 > argc)
		return usage(argv[0]);

	printf("recv: msg=%ukb, ", msg_size >> 10);
	if (use_splice) {
		printf("splice() ");
		if (splice_move)
			printf("zero map ");
		else
			printf("addr map ");
	} else
		printf("recv()");
	printf("\n");

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

	return recv_loop(connfd);
}
