#ifndef _SYS_RUMP_CPU_RNG_H_
#define _SYS_RUMP_CPU_RNG_H_

#include <sys/types.h>

#ifdef (__i386__) && (__HAVE_CPU_RNG)
#include <x86/cpu_rng.h>
#else
#define cpu_rng_init() 0
#define cpu_rng(a) 0
#endif /* __HAVE_CPU_RNG */

#endif /* _SYS_RUMP_CPU_RNG_H_ */
