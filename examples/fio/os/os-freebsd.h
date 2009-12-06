#ifndef FIO_OS_FREEBSD_H
#define FIO_OS_FREEBSD_H

#include <sys/sysctl.h>

#define FIO_HAVE_POSIXAIO
#define FIO_HAVE_ODIRECT

#define OS_MAP_ANON		MAP_ANON

typedef unsigned long os_cpu_mask_t;
typedef unsigned int os_random_state_t;

/*
 * FIXME
 */
static inline int blockdev_size(int fd, unsigned long long *bytes)
{
	return EINVAL;
}

static inline int blockdev_invalidate_cache(int fd)
{
	return EINVAL;
}

static inline unsigned long long os_phys_mem(void)
{
	int mib[2] = { CTL_HW, HW_PHYSMEM };
	unsigned long long mem;
	size_t len = sizeof(mem);

	sysctl(mib, 2, &mem, &len, NULL, 0);
	return mem;
}

static inline void os_random_seed(unsigned long seed, os_random_state_t *rs)
{
	srand(seed);
}

static inline long os_random_long(os_random_state_t *rs)
{
	long val;

	val = rand_r(rs);
	return val;
}

#endif
