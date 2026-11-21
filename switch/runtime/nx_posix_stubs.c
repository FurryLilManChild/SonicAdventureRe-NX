// SonicAdventureRe-nx: POSIX calls that only NVK's Rust standard library (inside libvulkan.a,
// from NAK, the shader compiler) names: files, processes, signals and threads it never reaches on
// the Switch. SA-nx's link dropped that code with --gc-sections; the game's image keeps every
// section (the game finds some statics by name), so these stand in. Each fails with ENOSYS, and
// the first 16 calls are logged, so a call that does happen shows.
//
// No headers: these are link stand-ins, and newlib declares some of the names differently.
#define ENOSYS 88
int *__errno(void);
void nx_posix_stub_called(const char *name);

#define STUB(name) \
    __attribute__((weak)) long name(void) \
    { \
        nx_posix_stub_called(#name); \
        *__errno() = ENOSYS; \
        return -1; \
    }

STUB(__res_init)
STUB(accept4)
STUB(chown)
STUB(chroot)
STUB(dirfd)
STUB(execvp)
STUB(fchmodat)
STUB(fchown)
STUB(fdatasync)
STUB(fdopendir)
STUB(flock)
STUB(fstatat64)
STUB(ftruncate64)
STUB(futimens)
STUB(getppid)
STUB(getpwuid_r)
STUB(getuid)
STUB(killpg)
STUB(lchown)
STUB(linkat)
STUB(lstat64)
STUB(mkdirat)
STUB(mkfifo)
STUB(mprotect)
STUB(openat64)
STUB(pause)
STUB(pipe2)
STUB(posix_spawn_file_actions_adddup2)
STUB(posix_spawn_file_actions_destroy)
STUB(posix_spawn_file_actions_init)
STUB(posix_spawnattr_destroy)
STUB(posix_spawnattr_init)
STUB(posix_spawnattr_setflags)
STUB(posix_spawnattr_setpgroup)
STUB(posix_spawnattr_setsigdefault)
STUB(posix_spawnp)
STUB(pread64)
STUB(preadv)
STUB(pthread_getattr_np)
STUB(pthread_setname_np)
STUB(pwrite64)
STUB(pwritev)
STUB(readdir64)
STUB(readv)
STUB(renameat)
STUB(sched_getaffinity)
STUB(sendfile64)
STUB(setgid)
STUB(setgroups)
STUB(setpgid)
STUB(setsid)
STUB(setuid)
STUB(sigaction)
STUB(sigaddset)
STUB(sigaltstack)
STUB(sigemptyset)
STUB(splice)
STUB(sysconf)
STUB(unlinkat)
STUB(utimensat)
STUB(waitid)
STUB(waitpid)

__attribute__((weak)) const char *gnu_get_libc_version(void) { return "newlib"; }
