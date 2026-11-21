/* SonicAdventureRe-nx: POSIX names the decomp's host code uses that devkitPro's newlib leaves
 * undeclared; switch/runtime/nx_libc.c defines them */
#ifndef SAREN_NX_POSIX_H
#define SAREN_NX_POSIX_H
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
int clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp, struct timespec *rmtp);
#ifdef __cplusplus
}
#endif
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif
#endif
