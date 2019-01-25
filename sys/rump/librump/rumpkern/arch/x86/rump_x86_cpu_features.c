/*	$NetBSD: rump_x86_cpu_features.c,v 1.1 2018/10/26 11:27:00 smichaels Exp $	*/

/*
 * Copyright (c) 2018-2019 NCC Group.  All Rights Reserved.
 *
 * Development of this software was supported by the
 * Finnish Cultural Foundation.
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
__KERNEL_RCSID(0, "$NetBSD: rump_x86_cpu_features.c,v 1.1 2018/10/26 11:27:00 smichaels Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>

#include <x86/specialreg.h>

#include <machine/cpufunc.h>
#include <machine/cpuvar.h>
#include <machine/cpu_features.h>

static void native_cpuid(unsigned *eax, unsigned *ebx,
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

unsigned long cpu_get_features(void) {
  unsigned long features = 0;
  unsigned eax, ebx, ecx, edx;

  eax = 0; ecx = 0;
  native_cpuid(&eax, &ebx, &ecx, &edx);

  const unsigned max_eax = eax;

  if (max_eax < 1)
    return features;

  eax = 1; ecx = 0;
  native_cpuid(&eax, &ebx, &ecx, &edx);
  features |= (ecx & CPUID2_RDRAND) ? CPU_FEATURE_RDRAND : 0;

  if (max_eax < 7)
    return features;

  eax = 7; ecx = 0;
  native_cpuid(&eax, &ebx, &ecx, &edx);
  features |= (ebx & CPUID_SEF_RDSEED) ? CPU_FEATURE_RDSEED : 0;

  return features;
}
