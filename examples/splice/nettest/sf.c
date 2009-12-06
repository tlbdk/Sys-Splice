/*
 * sendfile() a file
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/time.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <errno.h>

static int loop;

static inline int error(const char *n)
{
	perror(n);
	return -1;
}

static int usage(char *name)
{
	fprintf(stderr, "%s: file target port\n", name);
	return 1;
}

static int do_sf(int filefd, int sockfd)
{
	struct stat sb;
	int ret;

	if (fstat(filefd, &sb) < 0)
		return error("stat input file");
	if (!sb.st_size)
		return -1;

	if (lseek(filefd, 0, SEEK_SET) < 0)
		return error("lseek");

	while (sb.st_size) {
		ret = sendfile(sockfd, filefd, NULL, sb.st_size);
		if (ret < 0)
			return error("sendfile");
		else if (!ret)
			break;

		sb.st_size -= ret;
	}

	return 0;
}

static int parse_options(int argc, char *argv[])
{
	int c, index = 1;

	while ((c = getopt(argc, argv, "l")) != -1) {
		switch (c) {
		case 'l':
			loop = 1;
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
	int fd, filefd, index;

	if (argc < 4)
		return usage(argv[0]);

	index = parse_options(argc, argv);
	if (index == -1 || index + 1 > argc)
		return usage(argv[0]);

	filefd = open(argv[index], O_RDONLY);
	if (filefd < 0)
		return error("open input file");

	port = atoi(argv[index + 2]);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (inet_aton(argv[index + 1], &addr.sin_addr) != 1) {
		struct hostent *hent = gethostbyname(argv[index + 1]);

		if (!hent)
			return error("gethostbyname");

		memcpy(&addr.sin_addr, hent->h_addr, 4);
	}

	printf("Connecting to %s/%d\n", argv[index + 1], port);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return error("socket");

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
		return error("connect");

	do {
		if (do_sf(filefd, fd))
			break;
	} while (loop);

	close(fd);
	close(filefd);
	return 0;
}
