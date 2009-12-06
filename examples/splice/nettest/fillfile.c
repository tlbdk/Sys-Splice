#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "msg.h"
#include "crc32.h"
#include "../splice.h"

static unsigned int msg_size = 4096;
static unsigned int file_size = 128;
static unsigned long seed = 0x9e370001UL;

static int usage(const char *name)
{
	fprintf(stderr, "%s: [-s( msg size)] [-z(filesize (mb))] file\n", name);
	return 1;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "s:z:")) != -1) {
		switch (c) {
		case 's':
			msg_size = atoi(optarg);
			index++;
			break;
		case 'z':
			file_size = atoi(optarg);
			index++;
			break;
		default:
			return -1;
		}
	}

	printf("msg_size=%u, file_size=%umb\n", msg_size, file_size);
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

static int fill_file(int fd)
{
	struct msg *m = malloc(msg_size);
	unsigned long long fs = (unsigned long long) file_size * 1024 * 1024ULL;

	while (fs) {
		if (fs < msg_size)
			break;

		fill_buf(m, msg_size);
		write(fd, m, msg_size);
		fs -= msg_size;
	}

	close(fd);
	return 0;
}

int main(int argc, char *argv[])
{
	int fd, index;

	if (argc < 2)
		return usage(argv[0]);

	index = parse_options(argc, argv);
	if (index == -1 || index + 1 > argc)
		return usage(argv[0]);

	fd = open(argv[index], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return error("open output file");

	return fill_file(fd);
}
