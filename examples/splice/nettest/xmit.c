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
#include <sys/time.h>
#include <sys/resource.h>

#include "../splice.h"
#include "crc32.h"
#include "msg.h"

static void **buffers;
static int cur_buf, nr_bufs;

static unsigned int msg_size = 4096;
static int use_splice = 1;
static unsigned long packets = -1;

static unsigned long seed = 0x9e370001UL;

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

static int usage(char *name)
{
	fprintf(stderr, "%s: [-s(ize)] [-p(ackets to send)] [-n(ormal send())] target port\n", name);
	return 1;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "s:np:")) != -1) {
		switch (c) {
		case 's':
			msg_size = atoi(optarg);
			index++;
			break;
		case 'n':
			use_splice = 0;
			index++;
			break;
		case 'p':
			packets = atoi(optarg);
			index++;
			break;
		default:
			return -1;
		}
	}

	return index;
}

static void fill_buf(struct msg *m, unsigned int len)
{
	void *p = m;
	unsigned int left;
	unsigned long *val;

	m->msg_size = len;
	len -= sizeof(*m);
	p += sizeof(*m);

	left = len;
	val = p;
	while (left) {
		if (left < sizeof(*val))
			break;
		*val = rand() * seed;
		val++;
		left -= sizeof(*val);
	}

	m->crc32 = crc32(p, len);
}

static int splice_out(int sockfd, int pipefd, unsigned int len)
{
	while (len) {
		int ret = ssplice(pipefd, NULL, sockfd, NULL, len, 0);

		if (ret < 0)
			return error("splice to network");
		else if (!ret)
			break;

		len -= ret;
	}

	return len;
}

static int vmsplice_in(void *buf, int pipefd, unsigned int len)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};

	while (len) {
		int ret = svmsplice(pipefd, &iov, 1, 0);

		if (ret < 0)
			return error("vmsplice");
		else if (!ret)
			break;

		len -= ret;
		if (len) {
			iov.iov_base += ret;
			iov.iov_len -= ret;
		}
	}

	return len;
}

/*
 * Keep four pipes of buffers, that should be enough to ensure that
 * we don't reuse
 */
static void setup_buffers(void)
{
	int i;

	nr_bufs = 4 * SPLICE_SIZE / msg_size;

	buffers = malloc(sizeof(void *) * nr_bufs);

	for (i = 0; i < nr_bufs; i++)
		posix_memalign(&buffers[i], 4096, msg_size);
}

static void free_buffers(void)
{
	int i;

	for (i = 0; i < nr_bufs; i++)
		free(buffers[i]);

	free(buffers);
}

static int splice_send_loop(int fd)
{
	struct msg *m = NULL;
	int pipes[2];

	if (pipe(pipes) < 0)
		return error("pipe");

	setup_buffers();

	while (packets--) {
		m = buffers[cur_buf];
		if (++cur_buf == nr_bufs)
			cur_buf = 0;

		/*
		 * fill with random data and crc sum it
		 */
		fill_buf(m, msg_size);

		/*
		 * map data to our pipe
		 */
		if (vmsplice_in(m, pipes[1], msg_size))
			break;

		/*
		 * then transmit pipe to network
		 */
		if (splice_out(fd, pipes[0], msg_size))
			break;
	}

	free_buffers();
	close(pipes[0]);
	close(pipes[1]);
	return 0;
}

static int do_send(int fd, void *buf, unsigned int len)
{
	while (len) {
		int ret = send(fd, buf, len, 0);

		if (ret < 0)
			return error("send");
		else if (!ret)
			break;

		len -= ret;
		buf += ret;
	}

	return len;
}

static int normal_send_loop(int fd)
{
	struct msg *m;

	m = malloc(msg_size);

	while (packets--) {
		/*
		 * fill with random data and crc sum it
		 */
		fill_buf(m, msg_size);

		if (do_send(fd, m, msg_size))
			break;
	}

	free(m);
	return 0;
}

static int send_loop(int fd)
{
	struct rusage ru_s, ru_e;
	unsigned long ut, st, rt;
	struct timeval start;
	int ret;

	gettimeofday(&start, NULL);
	getrusage(RUSAGE_SELF, &ru_s);

	if (use_splice)
		ret = splice_send_loop(fd);
	else
		ret = normal_send_loop(fd);

	getrusage(RUSAGE_SELF, &ru_e);

	ut = mtime_since(&ru_s.ru_utime, &ru_e.ru_utime);
	st = mtime_since(&ru_s.ru_stime, &ru_e.ru_stime);
	rt = mtime_since_now(&start);

	printf("usr=%lu, sys=%lu, real=%lu\n", ut, st, rt);
	fflush(stdout);
	return ret;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	unsigned short port;
	int fd, index;

	if (argc < 3)
		return usage(argv[0]);

	index = parse_options(argc, argv);
	if (index == -1 || index + 1 > argc)
		return usage(argv[0]);

	port = atoi(argv[index + 1]);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (inet_aton(argv[index], &addr.sin_addr) != 1) {
		struct hostent *hent = gethostbyname(argv[index]);

		if (!hent)
			return error("gethostbyname");

		memcpy(&addr.sin_addr, hent->h_addr, 4);
	}

	printf("xmit: msg=%ukb, ", msg_size >> 10);
	if (use_splice)
		printf("vmsplice() -> splice()\n");
	else
		printf("send()\n");

	printf("Connecting to %s/%d\n", argv[index], port);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return error("socket");

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
		return error("connect");

	return send_loop(fd);
}
