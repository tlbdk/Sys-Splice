/*
 * Splice argument file to stdout
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "splice.h"

#ifndef BLKGETSIZE64
#define BLKGETSIZE64	_IOR(0x12,114,size_t)
#endif

static int usage(char *name)
{
	fprintf(stderr, "%s: infile | ...\n", name);
	return 1;
}

static long long in_size(int fd)
{
	unsigned long long bytes;
	struct stat sb;

	if (fstat(fd, &sb) < 0)
		return error("fstat");

	if (sb.st_size)
		return sb.st_size;

	if (ioctl(fd, BLKGETSIZE64, &bytes) < 0)
		return error("BLKGETSIZE64");

	return bytes;
}

int main(int argc, char *argv[])
{
	long long isize;
	int fd;

	if (argc < 2)
		return usage(argv[0]);

	if (check_output_pipe())
		return usage(argv[0]);

	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
		return error("open input");

	isize = in_size(fd);
	if (isize < 0)
		return isize;

	while (isize) {
		int ret = ssplice(fd, NULL, STDOUT_FILENO, NULL, isize, 0);

		if (ret < 0)
			return error("splice");
		else if (!ret)
			break;

		isize -= ret;
	}

	close(fd);
	return 0;
}
