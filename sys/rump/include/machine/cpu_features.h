#ifndef _SYS_RUMP_CPU_FEATURES_H_
#define _SYS_RUMP_CPU_FEATURES_H_

#include <sys/types.h>

enum cpu_features {
  CPU_FEATURE_RDRAND = 1,
  CPU_FEATURE_RDSEED = 2,
  CPU_FEATURE_NX = 4,
};

#if defined(__i386__) || defined(__x86_64__)
#include <x86/cpu_features.h>
#else
#define cpu_get_features() 0
#endif /* __HAVE_CPU_FEATURES */

#endif /* _SYS_RUMP_CPU_FEATURES_H_ */
