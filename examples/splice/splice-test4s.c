#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static int error(const char *n)
{
	perror(n);
	return -1;
}

static int parent(int listen_sk)
{
	unsigned long received;
	struct sockaddr addr;
	unsigned int len = sizeof(addr);
	int sk;

again:
	sk = accept(listen_sk, &addr, &len);
	if (sk < 0)
		return error("accept");

	/* read forever */
	received = 0;
	for (;;) {
		int ret = recv(sk, NULL, 128*1024*1024, MSG_TRUNC);
		if (ret > 0) {
			received += ret;
			continue;
		}
		if (!ret)
			break;
		if (errno == EAGAIN || errno == EINTR)
			continue;
		break;
	}

	printf("Received %f MB of data\n", (double) received / (1024*1024));
	close(sk);
	goto again;
}

int main(__attribute__((__unused__)) int argc, __attribute__((__unused__)) char **argv)
{
	struct sockaddr_in saddr_in;
	struct sockaddr addr;
	unsigned int len;
	int sk;

	signal(SIGCHLD, SIG_IGN);
	sk = socket(PF_INET, SOCK_STREAM, 0);
	if (sk < 0) {
		perror("socket");
		exit(1);
	}
	saddr_in.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr_in.sin_port = htons(1111);

	if (bind(sk, (struct sockaddr*)&saddr_in, sizeof(saddr_in)) < 0) {
                fprintf(stderr,"bind failed\n");
                exit(1);
        }

	if (listen(sk, 1) < 0) {
		perror("listen");
		exit(1);
	}
	len = sizeof(addr);
	if (getsockname(sk, &addr, &len) < 0) {
		perror("getsockname");
		exit(1);
	}
	return parent(sk);
}
