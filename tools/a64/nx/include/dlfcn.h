/* SonicAdventureRe-nx: the Switch loads no libraries by name; switch/runtime/nx_libc.c answers
 * every dlopen with null, which the PC backend's raster takes as no Vulkan there */
#ifndef SAREN_NX_DLFCN_H
#define SAREN_NX_DLFCN_H
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 0x100
#ifdef __cplusplus
extern "C" {
#endif
void *dlopen(const char *name, int flags);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
char *dlerror(void);
#ifdef __cplusplus
}
#endif
#endif
