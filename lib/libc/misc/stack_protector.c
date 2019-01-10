/*	$NetBSD: stack_protector.c,v 1.9 2013/08/19 22:14:37 matt Exp $	*/
/*	$OpenBSD: stack_protector.c,v 1.10 2006/03/31 05:34:44 deraadt Exp $	*/

/*
 * Copyright (c) 2002 Hiroaki Etoh, Federico G. Schwindt, and Miodrag Vallat.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: stack_protector.c,v 1.9 2013/08/19 22:14:37 matt Exp $");

#ifdef _LIBC
#include "namespace.h"
#endif
#include <sys/param.h>
#include <sys/sysctl.h>
#include <ssp/ssp.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#ifdef _LIBC
#include <syslog.h>
#include "extern.h"
#else
#define __sysctl sysctl
void xprintf(const char *fmt, ...);
#include <stdlib.h>
#endif

#include <immintrin.h>

#define     PAGE_SIZE       0x1000
#define     CPUID2_RDRAND   0x40000000

typedef union {
  struct {
    uintptr_t stack_chk_guard, heap_chunk_chk_guard, heap_page_chk_guard;
  } v;
  char __pad[PAGE_SIZE];
} stack_chk_guard_t;

stack_chk_guard_t __stack_chk_guard __attribute__((__aligned__(PAGE_SIZE)));

static void __fail(const char *) __attribute__((__noreturn__));
__dead void __stack_chk_fail_local(void);
void __guard_setup(void);

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

static int cpu_supports_rdrand(void) {
  unsigned eax, ebx, ecx, edx;

  eax = 0; ecx = 0;
  native_cpuid(&eax, &ebx, &ecx, &edx);

  const unsigned max_eax = eax;

  if (max_eax < 1)
    return 0;

  eax = 1; ecx = 0;
  native_cpuid(&eax, &ebx, &ecx, &edx);
  return (ecx & CPUID2_RDRAND);
}

static int do_rdrand(uintptr_t* result)
{
  int res = 0;
  while (res == 0)
    {
#ifdef __x86_64__
      res = _rdrand64_step((unsigned long long*)result);
#else
      res = _rdrand32_step((unsigned long*)result);
#endif
    }
  return (res == 1);
}

uintptr_t make_guard(void) {
  uintptr_t guard_tmp = 0;

	if (!cpu_supports_rdrand() || !do_rdrand(&guard_tmp)) {
		// If sysctl was unsuccessful, use the "terminator canary".
    unsigned char* p = (unsigned char*)&guard_tmp;
    p[0] = 0;
    p[1] = 0;
    p[2] = '\n';
    p[3] = 255;
	}
  
  // Put a null byte in the canary at the second byte.
  // See https://www.openwall.com/lists/kernel-hardening/2017/09/19/8
  ((unsigned char *)(void *)&guard_tmp)[1] = 0;

  return guard_tmp;
}

void __section(".text.startup")
__guard_setup(void)
{
  __stack_chk_guard.v.stack_chk_guard = make_guard();
  __stack_chk_guard.v.heap_chunk_chk_guard = make_guard();
  __stack_chk_guard.v.heap_page_chk_guard = make_guard();
}

/*ARGSUSED*/
static void
__fail(const char *msg)
{
#ifdef _LIBC
	struct syslog_data sdata = SYSLOG_DATA_INIT;
#endif
	struct sigaction sa;
	sigset_t mask;

	/* Immediately block all signal handlers from running code */
	(void)sigfillset(&mask);
	(void)sigdelset(&mask, SIGABRT);
	(void)sigprocmask(SIG_BLOCK, &mask, NULL);

#ifdef _LIBC
	/* This may fail on a chroot jail... */
	syslog_ss(LOG_CRIT, &sdata, "%s", msg);
#else
	xprintf("%s: %s\n", getprogname(), msg);
#endif

	(void)memset(&sa, 0, sizeof(sa));
	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_DFL;
	(void)sigaction(SIGABRT, &sa, NULL);
	(void)raise(SIGABRT);
	_exit(127);
}

void
__stack_chk_fail(void)
{
	__fail("stack overflow detected; terminated");
}

void
__chk_fail(void)
{
	__fail("buffer overflow detected; terminated");
}

void
__stack_chk_fail_local(void)
{
	__stack_chk_fail();
}
