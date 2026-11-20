// SonicAdventureRe-nx: Horizon memory probe. Answers, from a real launch, whether the game's
// 4 GB domain can sit at a fixed address: where the system put its regions, which 4 GB windows
// hold every range the game maps free, and whether memory can be mapped at an address we choose
// and seen at two addresses at once (the RAM mirrors). Writes sdmc:/switch/SonicAdventureRe-nx/probe.log.
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

static FILE *logf;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (logf) {
        va_start(ap, fmt);
        vfprintf(logf, fmt, ap);
        va_end(ap);
        fflush(logf);
    }
    consoleUpdate(NULL);
}

// the ranges, as offsets in the 4 GB window, that the decomp's runner and image use
static const u64 ranges[][2] = {
    {0x00400000, 0x05c00000}, // the program image (code and data, about 92 MB)
    {0x0c000000, 0x01000000}, {0x8c000000, 0x01000000}, {0xac000000, 0x01000000}, // RAM, mirrors
    {0xa0000000, 0x01000000}, {0xa4000000, 0x02000000}, {0x10000000, 0x02000000},
    {0x7c000000, 0x00010000}, {0xe0000000, 0x04000000}, {0xff000000, 0x01000000}, // devices
    {0x40000000, 0x01000000}, // the game's stack
    {0xb0000000, 0x00001000}, // the P2 calls
};
#define RANGES (sizeof ranges / sizeof ranges[0])

// is [at, at + size) entirely unmapped?
static bool is_free(u64 at, u64 size)
{
    u64 end = at + size;
    while (at < end) {
        MemoryInfo info;
        u32 page;
        if (R_FAILED(svcQueryMemory(&info, &page, at)))
            return false;
        if (info.type != MemType_Unmapped)
            return false;
        u64 next = info.addr + info.size;
        if (next <= at)
            return false;
        at = next;
    }
    return true;
}

static u64 info(u32 id)
{
    u64 v = 0;
    Result r = svcGetInfo(&v, id, CUR_PROCESS_HANDLE, 0);
    return R_SUCCEEDED(r) ? v : ~0ULL;
}

static void memory_map(void)
{
    u64 at = 0;
    int n = 0;
    say("\n-- memory map (mapped areas) --\n");
    for (;;) {
        MemoryInfo m;
        u32 page;
        if (R_FAILED(svcQueryMemory(&m, &page, at)))
            break;
        if (m.type != MemType_Unmapped && n < 60) {
            if (logf)
                fprintf(logf, "%010lx-%010lx type %2u perm %u\n", m.addr, m.addr + m.size, m.type, m.perm);
            n++;
        }
        u64 next = m.addr + m.size;
        if (next <= at || next == 0)
            break;
        at = next;
    }
    say("(%d mapped areas written to the log)\n", n);
}

static void map_tests(u64 window)
{
    Handle proc = envGetOwnProcessHandle();
    say("\n-- mapping tests in window %#lx --\n", window);
    say("own process handle: %s\n", proc != INVALID_HANDLE ? "yes" : "NO");
    if (proc == INVALID_HANDLE)
        return;
    const u64 size = 0x200000;
    void *src = memalign(0x1000, size);
    void *src2 = memalign(0x1000, size);
    if (!src || !src2) {
        say("memalign failed\n");
        return;
    }
    memset(src, 0x5a, size);
    u64 a = window + 0x0c000000, b = window + 0x8c000000, c = window + 0x00400000;

    // A: the same memory at two chosen addresses (the RAM mirrors)
    Result r1 = svcMapProcessMemory((void *)a, proc, (u64)src, size);
    say("A1 svcMapProcessMemory at %#lx: %#x\n", a, r1);
    Result r2 = svcMapProcessMemory((void *)b, proc, (u64)src, size);
    say("A2 same memory again at %#lx: %#x\n", b, r2);
    if (R_SUCCEEDED(r1) && R_SUCCEEDED(r2)) {
        *(volatile u32 *)a = 0x12345678;
        say("A3 wrote through %#lx, read through %#lx: %#x (want 0x12345678), heap copy %#x\n", a, b,
            *(volatile u32 *)b, *(volatile u32 *)src);
    }
    if (R_SUCCEEDED(r2))
        svcUnmapProcessMemory((void *)b, proc, (u64)src, size);
    if (R_SUCCEEDED(r1))
        svcUnmapProcessMemory((void *)a, proc, (u64)src, size);

    // B: code memory at a chosen address, made executable (the program image)
    static const u32 ret42[] = {0x52800540 /* mov w0, #42 */, 0xd65f03c0 /* ret */};
    memcpy(src2, ret42, sizeof ret42);
    armDCacheFlush(src2, size);
    Result r3 = svcMapProcessCodeMemory(proc, c, (u64)src2, size);
    say("B1 svcMapProcessCodeMemory at %#lx: %#x\n", c, r3);
    if (R_SUCCEEDED(r3)) {
        Result r4 = svcSetProcessMemoryPermission(proc, c, size, Perm_Rx);
        say("B2 make it executable: %#x\n", r4);
        if (R_SUCCEEDED(r4)) {
            armICacheInvalidate((void *)c, size);
            int v = ((int (*)(void))c)();
            say("B3 called code there: returned %d (want 42)\n", v);
        }
        Result r5 = svcSetProcessMemoryPermission(proc, c, size, Perm_Rw);
        say("B4 make it writable instead: %#x\n", r5);
        if (R_SUCCEEDED(r5)) {
            *(volatile u32 *)(c + 0x100) = 7;
            say("B5 wrote there and read back: %u\n", *(volatile u32 *)(c + 0x100));
        }
        Result r6 = svcUnmapProcessCodeMemory(proc, c, (u64)src2, size);
        say("B6 unmap: %#x\n", r6);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    mkdir("sdmc:/switch/SonicAdventureRe-nx", 0777);
    logf = fopen("sdmc:/switch/SonicAdventureRe-nx/probe.log", "a");
    say("SonicAdventureRe-nx memory probe\n");
    say("applet type %d (takeover is 0 or 1)\n", (int)appletGetAppletType());
    say("this program's code at %p\n", (void *)&main);
    say("heap block at %p\n", malloc(16));
    say("aslr  %#012lx size %#lx\n", info(InfoType_AslrRegionAddress), info(InfoType_AslrRegionSize));
    say("alias %#012lx size %#lx\n", info(InfoType_AliasRegionAddress), info(InfoType_AliasRegionSize));
    say("heap  %#012lx size %#lx\n", info(InfoType_HeapRegionAddress), info(InfoType_HeapRegionSize));
    say("stack %#012lx size %#lx\n", info(InfoType_StackRegionAddress), info(InfoType_StackRegionSize));

    say("\n-- 4 GB windows with every range the game maps free --\n");
    u64 aslr = info(InfoType_AslrRegionAddress), aslr_end = aslr + info(InfoType_AslrRegionSize);
    int free_count = 0;
    u64 first = 0;
    char line[600] = "";
    for (u64 w = 0; w < 128; w++) {
        u64 base = w << 32;
        bool ok = true;
        for (unsigned i = 0; ok && i < RANGES; i++) {
            u64 at = base + ranges[i][0];
            ok = at >= aslr && at + ranges[i][1] <= aslr_end && is_free(at, ranges[i][1]);
        }
        if (ok) {
            if (!free_count)
                first = base;
            free_count++;
            char item[8];
            snprintf(item, sizeof item, "%lu ", w);
            strncat(line, item, sizeof line - strlen(line) - 1);
        }
    }
    say("%d of 128 windows free: %s\n", free_count, line);
    say("window 1 (0x100000000, the current build's): %s\n", strstr(line, " 1 ") || !strncmp(line, "1 ", 2) ? "free" : "NOT free");

    memory_map();
    if (free_count)
        map_tests(first == 0 && free_count > 1 ? 1ULL << 32 : first);

    say("\nDone. The log is in sdmc:/switch/SonicAdventureRe-nx/probe.log\nPress + to exit.\n");
    if (logf)
        fprintf(logf, "\n");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    if (logf)
        fclose(logf);
    consoleExit(NULL);
    return 0;
}
