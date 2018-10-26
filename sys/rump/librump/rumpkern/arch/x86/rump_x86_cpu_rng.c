/*	$NetBSD: rump_x86_cpu_rng.c,v 1.1 2018/10/26 11:27:00 smichaels Exp $	*/

/*
 * Copyright (c) 2018 Spencer Michaels.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * __HAVE_CPU_RNG stubs.  It would be nice to have this
 * MI, but need MD for now because of inline-happy archs.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: rump_x86_cpu_rng.c,v 1.1 2018/10/26 11:27:00 smichaels Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>

#include <x86/specialreg.h>

#include <machine/cpufunc.h>
#include <machine/cpuvar.h>
#include <machine/cpu_rng.h>

void x86_enable_intr() {
  asm("sti");
}

void x86_disable_intr() {
  asm("cli");
}

u_long rcr0() {
  u_long cr0;
  asm("\t movq %%cr0, %0" : "=r"(cr0));
  return cr0;
}

void lcr0(u_long value) {
  asm("\t movq %0, %%cr0" : : "r"(value));
}

static inline void native_cpuid(unsigned *eax, unsigned *ebx,
                                unsigned *ecx, unsigned *edx)
{
  /* ecx is often an input as well as an output. */
  asm volatile("cpuid"
      : "=a" (*eax),
        "=b" (*ebx),
        "=c" (*ecx),
        "=d" (*edx)
      : "a" (*eax),
        "c" (*ecx));
}

static inline void get_cpu_features(bool *has_rdseed, bool *has_rdrand) {
  unsigned eax, ebx, ecx, edx;

  eax = 0; ecx = 0;
  native_cpuid(&eax, &ebx, &ecx, &edx);

  const unsigned max_eax = eax;
  

  if (max_eax >= 1) {
    eax = 1; ecx = 0;
    native_cpuid(&eax, &ebx, &ecx, &edx);
    *has_rdrand = (ecx & CPUID2_RDRAND) != 0;
  } else {
    *has_rdrand = false;
  }

  if (max_eax >= 7) {
    eax = 7; ecx = 0;
    native_cpuid(&eax, &ebx, &ecx, &edx);
    *has_rdseed = (ebx & CPUID_SEF_RDSEED) != 0;
  } else {
    *has_rdseed = false;
  }
}

static enum {
	CPU_RNG_NONE = 0,
	CPU_RNG_RDRAND,
	CPU_RNG_RDSEED,
	CPU_RNG_VIA
} cpu_rng_mode __read_mostly = CPU_RNG_NONE;

bool
cpu_rng_init(void)
{
  bool has_rdseed, has_rdrand;
  get_cpu_features(&has_rdseed, &has_rdrand);

	if (has_rdseed) {
		cpu_rng_mode = CPU_RNG_RDSEED;
		aprint_normal("cpu_rng: RDSEED\n");
		return true;
	} else if (has_rdrand) {
		cpu_rng_mode = CPU_RNG_RDRAND;
		aprint_normal("cpu_rng: RDRAND\n");
		return true;
	}
	return false;
}

static size_t
cpu_rng_rdrand(cpu_rng_t *out)
{
	uint8_t rndsts;

#ifdef __i386__
	uint32_t lo, hi;

	__asm __volatile("rdrand %0; setc %1" : "=r"(lo), "=qm"(rndsts));
	if (rndsts != 1)
		return 0;
	__asm __volatile("rdrand %0; setc %1" : "=r"(hi), "=qm"(rndsts));

	*out = (uint64_t)lo | ((uint64_t)hi << 32);
	explicit_memset(&lo, 0, sizeof(lo));
	explicit_memset(&hi, 0, sizeof(hi));
	if (rndsts != 1)
		return sizeof(lo) * NBBY;
#else
	__asm __volatile("rdrand %0; setc %1" : "=r"(*out), "=qm"(rndsts));
	if (rndsts != 1)
		return 0;
#endif
	return sizeof(*out) * NBBY;
}

static size_t
cpu_rng_rdseed(cpu_rng_t *out)
{
	uint8_t rndsts;

#ifdef __i386__
	uint32_t lo, hi;

	__asm __volatile("rdseed %0; setc %1" : "=r"(lo), "=qm"(rndsts));
        if (rndsts != 1)
		goto exhausted;
	__asm __volatile("rdseed %0; setc %1" : "=r"(hi), "=qm"(rndsts));
	if (rndsts != 1)
		goto exhausted;

	*out = (uint64_t)lo | ((uint64_t)hi << 32);
	explicit_memset(&lo, 0, sizeof(lo));
	explicit_memset(&hi, 0, sizeof(hi));
#else
	__asm __volatile("rdseed %0; setc %1" : "=r"(*out), "=qm"(rndsts));
#endif
	if (rndsts != 1)
		goto exhausted;

	return sizeof(*out) * NBBY;

	/*
	 * Userspace could have exhausted RDSEED, but the
	 * CPU-internal generator feeding RDRAND is guaranteed
	 * to be seeded even in this case.
	 */
exhausted:
	return cpu_rng_rdrand(out);
}

static size_t
cpu_rng_via(cpu_rng_t *out)
{
	uint32_t creg0, rndsts;

	/*
	 * Sadly, we have to monkey with the coprocessor enable and fault
	 * registers, which are really for the FPU, in order to read
	 * from the RNG.
	 *
	 * Don't remove CR0_TS from the call below -- comments in the Linux
	 * driver indicate that the xstorerng instruction can generate
	 * spurious DNA faults though no FPU or SIMD state is changed
	 * even if such a fault is generated.
	 *
	 * XXX can this really happen if we don't use "rep xstorrng"?
	 *
	 */
	kpreempt_disable();
	x86_disable_intr();
	creg0 = rcr0();
	lcr0(creg0 & ~(CR0_EM|CR0_TS)); /* Permit access to SIMD/FPU path */
	/*
	 * The VIA RNG has an output queue of 8-byte values.  Read one.
	 * This is atomic, so if the FPU were already enabled, we could skip
	 * all the preemption and interrupt frobbing.  If we had bread,
	 * we could have a ham sandwich, if we had any ham.
	 */
	__asm __volatile("xstorerng"
	    : "=a" (rndsts), "+D" (out) : "d" (0) : "memory");
	/* Put CR0 back how it was */
	lcr0(creg0);
	x86_enable_intr();
	kpreempt_enable();

	/*
	 * The Cryptography Research paper on the VIA RNG estimates
	 * 0.75 bits of entropy per output bit and advises users to
	 * be "even more conservative".
	 */
	return rndsts & 0xf ? 0 : sizeof(cpu_rng_t) * NBBY / 2;
}

size_t
cpu_rng(cpu_rng_t *out)
{
	switch (cpu_rng_mode) {
	case CPU_RNG_NONE:
		return 0;
	case CPU_RNG_RDSEED:
		return cpu_rng_rdseed(out);
	case CPU_RNG_RDRAND:
		return cpu_rng_rdrand(out);
	case CPU_RNG_VIA:
		return cpu_rng_via(out);
	default:
		panic("cpu_rng: unknown mode %d", (int)cpu_rng_mode);
	}
}
