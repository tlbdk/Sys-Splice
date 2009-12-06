#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "splice.h"

#define TARGET_HOSTNAME "localhost"

#define BYTES (128*1024*1024UL)
#define BUFSIZE (64*1024U)

#define NR (BYTES/BUFSIZE)

#define SENDFILE_LOOPS 10
#define SPLICE_LOOPS 10
#define SPLICE_PIPE_LOOPS 10

static int sendfile_loops = SENDFILE_LOOPS;
static int splice_pipe_loops = SPLICE_PIPE_LOOPS;
#if 0
static int splice_loops = SPLICE_LOOPS;
#endif

static volatile long long *cycles, cycles_per_sec;

static struct timeval start_time;
static double start_cycles;
static double cpu_pct;

static void start_timing(const char *desc)
{
	printf("%-20s: ", desc);
	fflush(stdout);
	gettimeofday(&start_time, NULL);
	/*
	 * Give the lowprio cycles thread a chance to run and thus
	 * we get an accurate timestamp:
	 */
	sched_yield();
	start_cycles = (double)*cycles;
}

static double end_timing(unsigned long long bytes, double *rate)
{
	static long long total;
	struct timeval end_time;
	double usecs;
	double end_cycles, cpu_cycles;

	gettimeofday(&end_time, NULL);
	end_cycles = (double)*cycles;

	usecs = (double) (end_time.tv_sec - start_time.tv_sec);
	usecs *= 1000000.0;
	usecs += (double) (end_time.tv_usec - start_time.tv_usec);
	total += bytes;

	cpu_cycles = end_cycles - start_cycles;
	cpu_pct = 100.0 -
		cpu_cycles / cycles_per_sec / ( usecs / 1000000.0 ) * 100.0;

	*rate = (double) bytes / usecs / (1024*1024) * 1000000;

	printf("%.2fMB/s (%.1fMB total, %.2f%% CPU)\n", *rate,
		(double) total / (1024*1024),
		cpu_pct
	);

	return cpu_pct;
}

static void calibrate_loops(void)
{
	long long l0, l1;
	int i;

	cycles_per_sec = 0;
	printf("calibrating cycles: "); fflush(stdout);

	/*
	 * Make sure we start on a precise timer IRQ boundary:
	 */
	usleep(50000);

	for (i = 0; i < 10; i++) {
		sched_yield();
		l0 = *cycles;
		usleep(200000);
		l1 = *cycles;
		cycles_per_sec = max(cycles_per_sec, l1-l0);
	}
	cycles_per_sec *= 5;

	printf("%Ld cycles/sec\n", cycles_per_sec);
}

static int child(void)
{
	static char buffer[BUFSIZE];
	int sk;
	double c1, c2, c3;
	int fd;
	struct sockaddr_in s_to;
	struct hostent *hp;
	double r1, r2, r3, r4, r5;
	unsigned int i;
	int pipefd[2];
	loff_t off = 0;

	r1 = r2 = r3 = r4 = r5 = 0;

	sk = socket(PF_INET, SOCK_STREAM, 0);
	if (!sk)
		return error("socket");
	hp = gethostbyname (TARGET_HOSTNAME);
	BUG_ON(!hp);
	bzero ((char *) &s_to, sizeof (s_to));
	bcopy ((char *) hp->h_addr, (char *) &(s_to.sin_addr), hp->h_length);
	s_to.sin_family = hp->h_addrtype;
	s_to.sin_port = htons(1111);

	calibrate_loops();

	fprintf(stdout, "BUFSIZE = %d\n", BUFSIZE);
	fflush(stdout);

	if (connect(sk, (struct sockaddr *)&s_to, sizeof(s_to)) < 0)
		return error("connect");

	start_timing("Empty buffer");
	for (i = 0; i < NR; i++)
		write(sk, buffer, BUFSIZE);
	end_timing(NR*BUFSIZE, &r1);

	fd = open("largefile", O_RDONLY);
	if (fd < 0)
		return error("largefile");

	start_timing("Read/write loop");
	for (i = 0; i < NR; i++) {
		if (read(fd, buffer, BUFSIZE) != BUFSIZE)
			return error("largefile read");
		write(sk, buffer, BUFSIZE);
	}
	end_timing(NR*BUFSIZE, &r2);
	close(fd);
	close(sk);

	start_timing("sendfile");
sendfile_again:
	sk = socket(PF_INET, SOCK_STREAM, 0);
	if (connect(sk, (struct sockaddr *)&s_to, sizeof(s_to)) < 0)
		return error("connect");

	fd = open("largefile", O_RDONLY);
	if (fd < 0)
		return error("largefile");

	i = NR*BUFSIZE;
	do {
		int ret = sendfile(sk, fd, NULL, i);
		i -= ret;
	} while (i);

	close(fd);
	close(sk);
	if (--sendfile_loops)
		goto sendfile_again;
	c1 = end_timing(NR*BUFSIZE*SENDFILE_LOOPS, &r3);

	start_timing("splice-pipe");
splice_pipe_again:
	sk = socket(PF_INET, SOCK_STREAM, 0);
	if (connect(sk, (struct sockaddr *)&s_to, sizeof(s_to)) < 0)
		return error("connect");

	fd = open("largefile", O_RDONLY);
	if (fd < 0)
		return error("largefile");
	if (pipe(pipefd) < 0)
		return error("pipe");

	i = NR*BUFSIZE;
	off = 0;
	do {
		int ret = ssplice(fd, &off, pipefd[1], NULL, min(i, BUFSIZE), SPLICE_F_NONBLOCK);
		if (ret <= 0)
			return error("splice-pipe-in");
		i -= ret;
		while (ret > 0) {
			int flags = i ? SPLICE_F_MORE : 0;
			int written = ssplice(pipefd[0], NULL, sk, NULL, ret, flags);
			if (written <= 0)
				return error("splice-pipe-out");
			ret -= written;
		}
	} while (i);

	close(fd);
	close(sk);
	close(pipefd[0]);
	close(pipefd[1]);
	if (--splice_pipe_loops)
		goto splice_pipe_again;
	c2 = end_timing(NR*BUFSIZE*SPLICE_LOOPS, &r4);

	/*
	 * Direct splicing was disabled as being immediately available,
	 * it's reserved for sendfile emulation now.
	 */
#if 0
	start_timing("splice");
splice_again:
	sk = socket(PF_INET, SOCK_STREAM, 0);
	if (connect(sk, (struct sockaddr *)&s_to, sizeof(s_to)) < 0)
		return error("connect");

	fd = open("largefile", O_RDONLY);
	if (fd < 0)
		return error("largefile");

	i = NR*BUFSIZE;
	off = 0;
	do {
		int flags = BUFSIZE < i ? SPLICE_F_MORE : 0;
		int ret;

		ret = ssplice(fd, &off, sk, NULL, min(i, BUFSIZE), flags);

		if (ret <= 0)
			return error("splice");
		i -= ret;
	} while (i);

	close(fd);
	close(sk);
	if (--splice_loops)
		goto splice_again;
	c3 = end_timing(NR*BUFSIZE*SPLICE_LOOPS, &r5);
#else
	c3 = 0;
#endif

	/*
	 * c1/r3 - sendfile
	 * c2/r4 - splice-pipe
	 * c3/r5 - splice
	 */

	if (c1 && c2)
		printf("sendfile is %.2f%% more efficient than splice-pipe.\n",
			(c2 - c1) / c1 * 100.0 );
	if (c1 && c3)
		printf("sendfile is %.2f%% more efficient than splice.\n",
			(c3 - c1) / c1 * 100.0 );
	if (c2 && c3)
		printf("splice is %.2f%% more efficient splice-pipe.\n",
			(c2 - c3) / c3 * 100.0 );
	if (r3 && r4)
		printf("sendfile is %.2f%% faster than splice-pipe.\n",
			(r3 - r4) / r4 * 100.0 );
	if (r3 && r5)
		printf("sendfile is %.2f%% faster than splice.\n",
			(r3 - r5) / r5 * 100.0 );
	if (r4 && r5)
		printf("splice is %.2f%% faster than splice-pipe.\n",
			(r5 - r4) / r4 * 100.0 );

	return 0;
}


static void setup_shared_var(void)
{
	char zerobuff [4096] = { 0, };
	int ret, fd;

	fd = creat(".tmp_mmap", 0700);
	BUG_ON(fd == -1);
	close(fd);

	fd = open(".tmp_mmap", O_RDWR|O_CREAT|O_TRUNC, 0666);
	BUG_ON(fd == -1);
	ret = write(fd, zerobuff, 4096);
	BUG_ON(ret != 4096);

	cycles = (void *)mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	BUG_ON(cycles == (void *)-1);

	close(fd);
}

#define SCHED_BATCH 3

#if defined(__i386__)
#define rdtscll(val)					\
do {							\
	__asm__ __volatile__("rdtsc" : "=A" (val));	\
} while (0)
#elif defined(__x86_64__)
#define rdtscll(val)						\
do {								\
	uint64_t lo, hi;					\
	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));	\
	(val) = (hi << 32) | lo;				\
} while (0)
#if 0
#elif defined(__ia64__)
#define rdtscll(val)					\
do {							\
	val = *__mm_clock_dev;				\
} while (0)
#endif
#else
#define rdtscll(val) \
	do { (val) = 0LL; } while (0)
#endif

/*
 * Keep lowprio looping - to meausure the number of idle cycles
 * available. It's tricky: we do a series of RDTSC calls, and
 * if the delay to the last measurement was less than 500 cycles,
 * we conclude that only this loop ran.
 */
static void lowprio_cycle_soak_loop(void)
{
        struct sched_param p = { sched_priority: 0 };
	unsigned long long t0, t1, delta;

	/*
	 * We are a nice +19 SCHED_BATCH task:
	 */
	BUG_ON(sched_setscheduler(0, SCHED_BATCH, &p) != 0);
	nice(40);

	rdtscll(t0);
	while (cycles >= 0) {
		rdtscll(t1);
		delta = t1-t0;
		if (delta < 500)
			*cycles += delta;
		t0 = t1;
	}
}

int main(__attribute__((__unused__)) int argc, __attribute__((__unused__)) char **argv)
{
	pid_t pid;

	setup_shared_var();

	signal(SIGCHLD, SIG_IGN);

	pid = fork();
	if (!pid) {
		lowprio_cycle_soak_loop();
		exit(0);
	}

	nice(-20);
	child();
	kill(pid, SIGHUP);
	exit(0);
}
