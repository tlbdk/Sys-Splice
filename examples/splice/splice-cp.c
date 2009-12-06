/*
 * Splice cp a file
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "splice.h"

#define BS	SPLICE_SIZE

static int splice_flags;

static int usage(char *name)
{
	fprintf(stderr, "%s: [-m] in_file out_file\n", name);
	return 1;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "m")) != -1) {
		switch (c) {
		case 'm':
			splice_flags = SPLICE_F_MOVE;
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
	int in_fd, out_fd, pfds[2], index;
	struct stat sb;

	index = parse_options(argc, argv);
	if (index == -1 || index + 2 > argc)
		return usage(argv[0]);

	in_fd = open(argv[index], O_RDONLY);
	if (in_fd < 0)
		return error("open input");

	if (fstat(in_fd, &sb) < 0)
		return error("stat input");

	out_fd = open(argv[index + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd < 0)
		return error("open output");

	if (pipe(pfds) < 0)
		return error("pipe");

	do {
		int this_len = min((off_t) BS, sb.st_size);
		int ret = ssplice(in_fd, NULL, pfds[1], NULL, this_len, 0);

		if (ret < 0)
			return error("splice-in");
		else if (!ret)
			break;

		sb.st_size -= ret;
		while (ret > 0) {
			int written = ssplice(pfds[0], NULL, out_fd, NULL, ret, splice_flags);
			if (written <= 0)
				return error("splice-out");
			ret -= written;
		}
	} while (sb.st_size);

	close(in_fd);
	close(pfds[1]);
	close(out_fd);
	close(pfds[0]);
	return 0;
}
