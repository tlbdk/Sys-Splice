/*
 * Splice stdin to file
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "splice.h"

static int splice_flags;
static unsigned int splice_size = SPLICE_SIZE;

static int usage(char *name)
{
	fprintf(stderr, "... | %s: [-m] [-s splice size] out_file\n", name);
	return 1;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "ms:")) != -1) {
		switch (c) {
		case 'm':
			splice_flags = SPLICE_F_MOVE;
			index++;
			break;
		case 's':
			splice_size = atoi(optarg);
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
	int fd, index;

	if (check_input_pipe())
		return usage(argv[0]);

	index = parse_options(argc, argv);
	if (index == -1 || index + 1 > argc)
		return usage(argv[0]);

	fd = open(argv[index], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return error("open");

	do {
		int ret = ssplice(STDIN_FILENO, NULL, fd, NULL, splice_size, splice_flags);

		if (ret < 0)
			return error("splice");
		else if (!ret)
			break;
	} while (1);

	close(fd);
	return 0;
}
