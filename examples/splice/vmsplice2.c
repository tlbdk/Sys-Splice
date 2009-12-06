/*
 * Use vmsplice to fill some user memory into a pipe. vmsplice writes
 * to stdout, so that must be a pipe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <sys/poll.h>
#include <sys/types.h>

#include "splice.h"

#define PAGE_SIZE	4096
#define PAGE_MASK	(PAGE_SIZE - 1)

static int alloc_stack;
static int cross_page;
static int force_align;
static int full_page;
static int gift;

int do_vmsplice(int fd, struct iovec *iov, unsigned long nr_vecs)
{
	struct pollfd pfd = { .fd = fd, .events = POLLOUT, };
	long written;

	while (nr_vecs) {
		/*
		 * in a real app you'd be more clever with poll of course,
		 * here we are basically just blocking on output room and
		 * not using the free time for anything interesting.
		 */
		if (poll(&pfd, 1, -1) < 0)
			return error("poll");

		written = svmsplice(fd, iov, nr_vecs, gift ? SPLICE_F_GIFT : 0);

		if (written <= 0)
			return error("vmsplice");

		while (written) {
			int this_len = iov->iov_len;

			if (this_len > written)
				this_len = written;

			iov->iov_len -= this_len;
			if (!iov->iov_len) {
				nr_vecs--;
				iov++;
			}

			written -= this_len;
		}
	}

	return 0;
}

static int usage(char *name)
{
	fprintf(stderr, "%s [-s(tack)] [-c(ross page)] [-a(lign)] [-f(ull page)] [-g(ift)] | ...\n", name);
	return 1;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "scafg")) != -1) {
		switch (c) {
		case 's':
			alloc_stack = 1;
			index++;
			break;
		case 'c':
			cross_page = 1;
			index++;
			break;
		case 'a':
			force_align = 1;
			index++;
			break;
		case 'f':
			full_page = 1;
			index++;
			break;
		case 'g':
			gift = 1;
			index++;
			break;
		default:
			return -1;
		}
	}

	if (cross_page && force_align) {
		fprintf(stderr, "Can't get both aligning and cross page spanning\n");
		return -1;
	}

	return index;
}

#define ASIZE	(3 * PAGE_SIZE)
#define S1	"header header header header header header header header "
#define S2	"body body body body body body body body body body body "
#define S3	"footer footer footer footer footer footer footer footer"

static void check_address(void *addr, void *start, void *end, int len,char *msg)
{
	if (addr < start || (addr + len - 1) > end)
		fprintf(stderr, "%s: bad: %p < %p < %p false\n", msg, start, addr, end);
}

int main(int argc, char *argv[])
{
	struct iovec vecs[3];
	char stack1[ASIZE], stack2[ASIZE], stack3[ASIZE];
	char *h_s, *h_e, *b_s, *b_e, *f_s, *f_e;
	char *h, *b, *f;

	if (check_output_pipe())
		return usage(argv[0]);

	if (parse_options(argc, argv) < 0)
		return usage(argv[0]);

	if (alloc_stack) {
		h = stack1;
		b = stack2;
		f = stack3;
	} else {
		h = malloc(ASIZE);
		b = malloc(ASIZE);
		f = malloc(ASIZE);
	}

	memset(h, 0, ASIZE);
	memset(b, 0, ASIZE);
	memset(f, 0, ASIZE);

	h_s = h;
	h_e = h_s + 2 * PAGE_SIZE - 1;
	b_s = b;
	b_e = b_s + 2 * PAGE_SIZE - 1;
	f_s = f;
	f_e = f_s + 2 * PAGE_SIZE - 1;

	if (force_align || cross_page) {
		/* align forward to start of 2nd page */
		unsigned long off;

		off = PAGE_SIZE - ((unsigned long) h & PAGE_MASK);
		h += off;
		off = PAGE_SIZE - ((unsigned long) b & PAGE_MASK);
		b += off;
		off = PAGE_SIZE - ((unsigned long) f & PAGE_MASK);
		f += off;
		if (cross_page) {
			/* this puts half the string in both pages */
			h -= strlen(S1) / 2;
			b -= strlen(S2) / 2;
			f -= strlen(S3) / 2;
		}
	}

	strcpy(h, S1);
	strcpy(b, S2);
	strcpy(f, S3);

	vecs[0].iov_base = h;
	vecs[1].iov_base = b;
	vecs[2].iov_base = f;
	if (!full_page) {
		vecs[0].iov_len = strlen(vecs[0].iov_base);
		vecs[1].iov_len = strlen(vecs[1].iov_base);
		vecs[2].iov_len = strlen(vecs[2].iov_base);
	} else {
		vecs[0].iov_len = PAGE_SIZE;
		vecs[1].iov_len = PAGE_SIZE;
		vecs[2].iov_len = PAGE_SIZE;
	}

	check_address(h, h_s, h_e, vecs[0].iov_len, "header");
	check_address(b, b_s, b_e, vecs[1].iov_len, "body");
	check_address(f, f_s, f_e, vecs[2].iov_len, "footer");

	return do_vmsplice(STDOUT_FILENO, vecs, 3);
}
