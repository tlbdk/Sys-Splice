/*
 * Use vmsplice to splice data from a pipe to user space memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/types.h>

#include "splice.h"

static int do_dump;
static int do_ascii;
static int do_zeromap;
static int splice_flags;
static unsigned int splice_size = SPLICE_SIZE;

static int do_vmsplice_unmap(int fd, unsigned char *buf, int len)
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};

	return svmsplice(fd, &iov, 1, SPLICE_F_UNMAP);
}

static int do_vmsplice(int fd, void **buf, int len)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN, };
	struct iovec iov = {
		.iov_base = *buf,
		.iov_len = len,
	};
	int written;
	int ret = 0;

	while (len) {
		/*
		 * in a real app you'd be more clever with poll of course,
		 * here we are basically just blocking on output room and
		 * not using the free time for anything interesting.
		 */
		if (poll(&pfd, 1, -1) < 0)
			return error("poll");

		written = svmsplice(fd, &iov, 1, splice_flags);

		if (!ret)
			*buf = iov.iov_base;

		if (written < 0)
			return error("vmsplice");
		else if (!written)
			break;

		len -= written;
		ret += written;
		if (len) {
			iov.iov_len -= written;
			iov.iov_base += written;
		}
	}

	return ret;
}

static int usage(char *name)
{
	fprintf(stderr, "| %s [-s(ize)] [-d(ump)] [-a(ascii)] [-m(ap)] [-z(eromap)] [-n non-block]\n", name);
	return 1;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "admzs:n")) != -1) {
		switch (c) {
		case 'a':
			do_ascii = 1;
			index++;
			break;
		case 'd':
			do_dump = 1;
			index++;
			break;
		case 'm':
			splice_flags |= SPLICE_F_MOVE;
			index++;
			break;
		case 'z':
			do_zeromap = 1;
			index++;
			break;
		case 's':
			splice_size = atoi(optarg);
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

	if (do_zeromap && !(splice_flags & SPLICE_F_MOVE)) {
		fprintf(stderr, "zero map only valid for -m(ove)\n");
		return -1;
	}

	return index;
}

static void hexdump(unsigned char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		printf("%02x", buf[i]);
}

static void asciidump(unsigned char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		printf("%c", buf[i]);
}

int main(int argc, char *argv[])
{
	void *buf;
	int ret;

	if (parse_options(argc, argv) < 0)
		return usage(argv[0]);

	if (check_input_pipe())
		return usage(argv[0]);

	if (!do_zeromap) {
		buf = malloc(splice_size);
		memset(buf, 0, splice_size);
	} else
		buf = NULL;

	ret = do_vmsplice(STDIN_FILENO, &buf, splice_size);
	if (ret < 0)
		return 1;
	else if (!ret)
		return 0;

	if (do_dump)
		hexdump(buf, ret);
	if (do_ascii)
		asciidump(buf, ret);

	if (splice_flags & SPLICE_F_MOVE) {
		ret = do_vmsplice_unmap(STDIN_FILENO, buf, ret);
		if (ret < 0)
			perror("vmsplice");
	}

	return ret;
}
