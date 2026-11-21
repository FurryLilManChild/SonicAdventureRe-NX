// SonicAdventureRe-nx: the few POSIX calls the decomp's PC backend and play.c make that newlib
// and libnx don't give.
#include <errno.h>
#include <malloc.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <switch.h>

// a sleep to a time on the monotonic clock (TIMER_ABSTIME) or for a time
int clock_nanosleep(clockid_t clock, int flags, const struct timespec *t, struct timespec *left)
{
    (void)clock;
    (void)left;
    s64 ns = (s64)t->tv_sec * 1000000000LL + t->tv_nsec;
    if (flags & 1) { // TIMER_ABSTIME
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        ns -= (s64)now.tv_sec * 1000000000LL + now.tv_nsec;
    }
    if (ns > 0)
        svcSleepThread(ns);
    return 0;
}

// weak: NVK (libvulkan.a) brings its own
__attribute__((weak)) int posix_memalign(void **out, size_t alignment, size_t size)
{
    void *p = memalign(alignment, size);
    if (!p)
        return ENOMEM;
    *out = p;
    return 0;
}

// the backend's raster opens a Vulkan library by name and takes vkGetInstanceProcAddr from it.
// On the Switch Vulkan is NVK, linked in (libvulkan.a), so any name opens it, and that call is
// the one it gives
void *vkGetInstanceProcAddr(void *instance, const char *name);
static char nx_vulkan_handle;

void *dlopen(const char *name, int flags)
{
    (void)name;
    (void)flags;
    return &nx_vulkan_handle;
}

void *dlsym(void *handle, const char *name)
{
    if (handle == &nx_vulkan_handle && name && !strcmp(name, "vkGetInstanceProcAddr"))
        return (void *)vkGetInstanceProcAddr;
    return NULL;
}

int dlclose(void *handle)
{
    (void)handle;
    return 0;
}

char *dlerror(void) { return "no dynamic libraries on the Switch"; }

// nx_posix_stubs.c's calls: the first 16 are logged, so one that runs shows
#include <stdio.h>
void nx_posix_stub_called(const char *name)
{
    static int logged;
    if (logged < 16) {
        logged++;
        fprintf(stderr, "nx: %s called, which the Switch lacks (ENOSYS)\n", name);
    }
}
