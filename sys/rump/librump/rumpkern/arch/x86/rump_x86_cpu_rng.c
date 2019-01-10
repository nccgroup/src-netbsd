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
#include <machine/cpu_features.h>
#include <machine/cpu_rng.h>

static enum {
	CPU_RNG_NONE = 0,
	CPU_RNG_RDRAND,
	CPU_RNG_RDSEED
} cpu_rng_mode __read_mostly = CPU_RNG_NONE;

bool
cpu_rng_init(void)
{
  unsigned long features = cpu_get_features();

	if (features & CPU_FEATURE_RDSEED) {
		cpu_rng_mode = CPU_RNG_RDSEED;
		aprint_normal("cpu_rng: RDSEED\n");
		return true;
	} else if (features & CPU_FEATURE_RDRAND) {
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
	default:
		panic("cpu_rng: unknown mode %d", (int)cpu_rng_mode);
	}
}
