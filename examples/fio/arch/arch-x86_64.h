#ifndef ARCH_X86_64_h
#define ARCH_X86_64_h

#define ARCH	(arch_x86_64)

#ifndef __NR_ioprio_set
#define __NR_ioprio_set		251
#define __NR_ioprio_get		252
#endif

#ifndef __NR_fadvise64
#define __NR_fadvise64		221
#endif

#ifndef __NR_sys_splice
#define __NR_sys_splice		275
#define __NR_sys_tee		276
#define __NR_sys_vmsplice	278
#endif

#ifndef __NR_async_exec
#define __NR_async_exec		286
#define __NR_async_wait		287
#define __NR_umem_add		288
#define __NR_async_thread	289
#endif

#define	FIO_HUGE_PAGE		2097152

#define FIO_HAVE_SYSLET

#define nop		__asm__ __volatile__("rep;nop": : :"memory")
#define read_barrier()	__asm__ __volatile__("lfence":::"memory")
#define write_barrier()	__asm__ __volatile__("sfence":::"memory")

static inline unsigned int arch_ffz(unsigned int bitmask)
{
	__asm__("bsfl %1,%0" :"=r" (bitmask) :"r" (~bitmask));
	return bitmask;
}
#define ARCH_HAVE_FFZ
#define ARCH_HAVE_SSE

#endif
