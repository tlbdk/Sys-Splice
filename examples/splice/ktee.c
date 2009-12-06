/*
 * A tee implementation using sys_tee. Stores stdin input in the given file
 * and duplicates that to stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

#include "splice.h"

static int splice_flags;

static int do_splice(int infd, int outfd, unsigned int len, char *msg)
{
	while (len) {
		int written = ssplice(infd, NULL, outfd, NULL, len, splice_flags);

		if (written <= 0)
			return error(msg);

		len -= written;
	}

	return 0;
}

static int usage(char *name)
{
	fprintf(stderr, "... | %s: [-m(ove)] outfile\n", name);
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
	int fd, index;

	if (argc < 2)
		return usage(argv[0]);

	if (check_input_pipe())
		return usage(argv[0]);

	index = parse_options(argc, argv);
	if (index == -1 || index + 1 > argc)
		return usage(argv[0]);

	fd = open(argv[index], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return error("open output");

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
		if (do_splice(STDIN_FILENO, fd, tee_len, "splice-file"))
			break;
	} while (1);

	return 0;
}
