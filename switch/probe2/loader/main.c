// SonicAdventureRe-nx probe 2, the loader. In one launch it checks the three things the Switch
// build's design rests on, each written to sdmc:/switch/SonicAdventureRe-nx/probe.log:
//   1. a 4 GB window chosen at start that avoids the heap and alias regions, not just mapped pages;
//   2. one block of memory seen at three addresses (the game's RAM and its two mirrors), through
//      code memory, as libnx's Jit makes its second view;
//   3. a program image linked for a fixed window, moved to the chosen one and started with its own
//      libnx (the payload, built in, which then prints and logs).
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

extern const unsigned char payload_start[], payload_end[];
__asm__(".section .rodata\n.balign 4096\n.global payload_start\npayload_start:\n"
        ".incbin \"" PAYLOAD_PATH "\"\n.global payload_end\npayload_end:\n.balign 8\n.text\n");

#define CARVE (160ULL << 20) // the heap's end, kept for the image (the game's is about 92 MB)

void __appExit(void); // libnx: closes the services __appInit opened

static FILE *logf;
static void *g_ctx, *g_saved_lr;
static u64 g_carve, g_carve_size;

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

// libnx's start hands envSetup what the launcher gave: keep it, to give the payload the same
void __real_envSetup(void *ctx, Handle main_thread, LoaderReturnFn saved_lr);
void __wrap_envSetup(void *ctx, Handle main_thread, LoaderReturnFn saved_lr)
{
    g_ctx = ctx;
    g_saved_lr = (void *)saved_lr;
    __real_envSetup(ctx, main_thread, saved_lr);
}

// newlib's heap is the launcher's heap less its last CARVE bytes, which hold the payload's image
extern char *fake_heap_start, *fake_heap_end;
void __libnx_initheap(void)
{
    if (!envHasHeapOverride())
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
    u64 addr = (u64)envGetHeapOverrideAddr(), size = envGetHeapOverrideSize();
    g_carve = (addr + size - CARVE) & ~0xfffffULL;
    g_carve_size = addr + size - g_carve;
    fake_heap_start = (char *)addr;
    fake_heap_end = (char *)g_carve;
}

static u64 info(u32 id)
{
    u64 v = 0;
    return R_SUCCEEDED(svcGetInfo(&v, id, CUR_PROCESS_HANDLE, 0)) ? v : ~0ULL;
}

static bool unmapped(u64 at, u64 size)
{
    u64 end = at + size;
    while (at < end) {
        MemoryInfo m;
        u32 page;
        if (R_FAILED(svcQueryMemory(&m, &page, at)) || m.type != MemType_Unmapped)
            return false;
        u64 next = m.addr + m.size;
        if (next <= at)
            return false;
        at = next;
    }
    return true;
}

static bool overlaps(u64 a, u64 as, u64 b, u64 bs) { return a < b + bs && b < a + as; }

static const u64 ranges[][2] = {
    {0x00400000, 0x05c00000}, {0x0c000000, 0x01000000}, {0x8c000000, 0x01000000},
    {0xac000000, 0x01000000}, {0xa0000000, 0x01000000}, {0xa4000000, 0x02000000},
    {0x10000000, 0x02000000}, {0x7c000000, 0x00010000}, {0xe0000000, 0x04000000},
    {0xff000000, 0x01000000}, {0x40000000, 0x01000000}, {0xb0000000, 0x00001000},
};

static bool window_ok(u64 w)
{
    u64 base = w << 32;
    u64 aslr = info(InfoType_AslrRegionAddress), aslr_size = info(InfoType_AslrRegionSize);
    u64 heap = info(InfoType_HeapRegionAddress), heap_size = info(InfoType_HeapRegionSize);
    u64 alias = info(InfoType_AliasRegionAddress), alias_size = info(InfoType_AliasRegionSize);
    u64 stack = info(InfoType_StackRegionAddress), stack_size = info(InfoType_StackRegionSize);
    for (unsigned i = 0; i < sizeof ranges / sizeof ranges[0]; i++) {
        u64 at = base + ranges[i][0], size = ranges[i][1];
        if (at < aslr || at + size > aslr + aslr_size || overlaps(at, size, heap, heap_size) ||
            overlaps(at, size, alias, alias_size) || overlaps(at, size, stack, stack_size) ||
            !unmapped(at, size))
            return false;
    }
    return true;
}

static void mirror_test(u64 base)
{
    Handle proc = envGetOwnProcessHandle();
    const u64 size = 0x01000000;
    u64 v0 = base + 0x0c000000, v1 = base + 0x8c000000, v2 = base + 0xac000000;
    say("\n-- 2. game RAM at three addresses (window %#lx) --\n", base);
    void *src = memalign(0x1000, size);
    if (!src) {
        say("memalign failed\n");
        return;
    }
    memset(src, 0, size);
    armDCacheFlush(src, size);
    Result r = svcMapProcessCodeMemory(proc, v0, (u64)src, size);
    say("code memory at %#lx: %#x\n", v0, r);
    if (R_FAILED(r))
        return;
    Result rp = svcSetProcessMemoryPermission(proc, v0, size, Perm_Rw);
    say("made writable: %#x\n", rp);
    Result r1 = svcMapProcessMemory((void *)v1, proc, v0, size);
    say("second view at %#lx: %#x\n", v1, r1);
    Result r2 = svcMapProcessMemory((void *)v2, proc, v0, size);
    say("third view at %#lx: %#x\n", v2, r2);
    if (R_SUCCEEDED(rp) && R_SUCCEEDED(r1) && R_SUCCEEDED(r2)) {
        volatile u32 *a = (u32 *)v0, *b = (u32 *)v1, *c = (u32 *)v2;
        a[100] = 0x11111111;
        u32 b1 = b[100], c1 = c[100];
        b[200] = 0x22222222;
        u32 a2 = a[200], c2 = c[200];
        c[0x3fffff] = 0x33333333; // the last word
        u32 a3 = a[0x3fffff], b3 = b[0x3fffff];
        bool ok = b1 == 0x11111111 && c1 == 0x11111111 && a2 == 0x22222222 && c2 == 0x22222222 &&
                  a3 == 0x33333333 && b3 == 0x33333333;
        say("wrote through each view, read through the others: %s (%#x %#x / %#x %#x / %#x %#x)\n",
            ok ? "ALL MATCH" : "MISMATCH", b1, c1, a2, c2, a3, b3);
    } else if (R_SUCCEEDED(rp) && (R_FAILED(r1) || R_FAILED(r2))) {
        // the other order: views first, while the code memory is still as mapped
        if (R_SUCCEEDED(r1))
            svcUnmapProcessMemory((void *)v1, proc, v0, size);
        if (R_SUCCEEDED(r2))
            svcUnmapProcessMemory((void *)v2, proc, v0, size);
        Result rx = svcSetProcessMemoryPermission(proc, v0, size, Perm_Rx);
        Result r1b = svcMapProcessMemory((void *)v1, proc, v0, size);
        say("variant (code memory left executable, as libnx's Jit): perm %#x, view %#x\n", rx, r1b);
        if (R_SUCCEEDED(r1b)) {
            volatile u32 *b = (u32 *)v1;
            b[7] = 0x44444444;
            say("  wrote through the view, read back: %#x\n", b[7]);
            svcUnmapProcessMemory((void *)v1, proc, v0, size);
        }
        r1 = r2 = 1;
    }
    if (R_SUCCEEDED(r2) && r2 != 1)
        say("unmap third: %#x\n", svcUnmapProcessMemory((void *)v2, proc, v0, size));
    if (R_SUCCEEDED(r1) && r1 != 1)
        say("unmap second: %#x\n", svcUnmapProcessMemory((void *)v1, proc, v0, size));
    svcSetProcessMemoryPermission(proc, v0, size, Perm_Rw);
    say("unmap code memory: %#x\n", svcUnmapProcessCodeMemory(proc, v0, (u64)src, size));
}

struct image_header {
    char magic[8];
    u64 link_base, start, end, entry;
    u32 segments, relocations, flags, pad;
    u64 symbol;
};
struct image_segment {
    u64 offset, filesz, memsz;
    u32 flags, pad;
};

static bool load_payload(u64 base)
{
    Handle proc = envGetOwnProcessHandle();
    const struct image_header *h = (const void *)payload_start;
    say("\n-- 3. the payload: an image linked for a fixed window, moved and started --\n");
    if (memcmp(h->magic, "SAREIMG1", 8)) {
        say("bad image\n");
        return false;
    }
    u64 size = h->end - h->start;
    u64 at = base + (h->start - h->link_base);
    u64 delta = base - h->link_base;
    say("image: %#lx bytes, linked at %#lx, going to %#lx, %u addresses to move\n", size, h->start, at,
        h->relocations);
    if (size > g_carve_size) {
        say("too big for the kept heap\n");
        return false;
    }
    u8 *dst = (u8 *)g_carve;
    memset(dst, 0, size);
    const struct image_segment *seg = (const void *)(h + 1);
    const u8 *data = (const u8 *)(seg + h->segments);
    for (u32 i = 0; i < h->segments; i++) {
        memcpy(dst + seg[i].offset, data, seg[i].filesz);
        data += (seg[i].filesz + 7) & ~7ULL;
    }
    const u32 *rel = (const u32 *)data;
    for (u32 i = 0; i < h->relocations; i++)
        *(u64 *)(dst + rel[i]) += delta;
    struct {
        u64 magic, window, link_window, relocations;
    } *hand = (void *)(dst + h->symbol);
    if (h->symbol) {
        hand->window = base;
        hand->link_window = h->link_base;
        hand->relocations = h->relocations;
    }
    armDCacheFlush(dst, size);
    Result r = svcMapProcessCodeMemory(proc, at, (u64)dst, size);
    say("mapped at %#lx: %#x\n", at, r);
    if (R_FAILED(r))
        return false;
    for (u32 i = 0; i < h->segments; i++) {
        u64 s = at + (seg[i].offset & ~0xfffULL);
        u64 e = (at + seg[i].offset + seg[i].memsz + 0xfff) & ~0xfffULL;
        u32 perm = (seg[i].flags & 1) ? Perm_Rx : (seg[i].flags & 2) ? Perm_Rw : Perm_R;
        Result rp = svcSetProcessMemoryPermission(proc, s, e - s, perm);
        say("segment %u at %#lx, %#lx bytes, permission %u: %#x\n", i, s, e - s, perm, rp);
        if (R_FAILED(rp))
            return false;
    }
    armICacheInvalidate((void *)at, size);
    return true;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    mkdir("sdmc:/switch/SonicAdventureRe-nx", 0777);
    logf = fopen("sdmc:/switch/SonicAdventureRe-nx/probe.log", "a");
    say("SonicAdventureRe-nx memory probe 2\n");
    say("applet type %d, own process handle %s\n", (int)appletGetAppletType(),
        envGetOwnProcessHandle() != INVALID_HANDLE ? "yes" : "NO");
    say("heap  %#012lx size %#lx\nalias %#012lx size %#lx\n", info(InfoType_HeapRegionAddress),
        info(InfoType_HeapRegionSize), info(InfoType_AliasRegionAddress), info(InfoType_AliasRegionSize));
    say("kept for the image: %#lx, %lu MB\n", g_carve, g_carve_size >> 20);

    say("\n-- 1. windows clear of the heap and alias regions and of mapped pages --\n");
    const struct image_header *h = (const void *)payload_start;
    u64 link_window = h->link_base >> 32, chosen = 0;
    char list[128] = "";
    for (u64 w = 1; w < 16; w++)
        if (window_ok(w)) {
            char item[8];
            snprintf(item, sizeof item, "%lu ", w);
            strncat(list, item, sizeof list - strlen(list) - 1);
            // prefer a window other than the link's, so moving the image is really tested
            if (!chosen || (chosen == link_window && w != link_window))
                chosen = w;
        }
    say("usable windows: %s\nchosen: %lu (the payload was linked for %lu)\n", list, chosen, link_window);
    if (!chosen) {
        say("no usable window\n");
    } else {
        mirror_test(chosen << 32);
        if (load_payload(chosen << 32)) {
            // the launcher's entries for the payload, the heap less the image's part
            static ConfigEntry entries[64];
            const ConfigEntry *e = g_ctx;
            int n = 0;
            for (; e && e->Key != EntryType_EndOfList && n < 63; e++, n++) {
                entries[n] = *e;
                if (e->Key == EntryType_OverrideHeap)
                    entries[n].Value[1] = g_carve - e->Value[0];
            }
            entries[n].Key = EntryType_EndOfList;
            u64 entry = (chosen << 32) + (h->entry - h->link_base);
            say("starting the payload at %#lx\n", entry);
            if (logf)
                fclose(logf);
            consoleExit(NULL);
            __appExit();
            register u64 x0 __asm__("x0") = (u64)(g_ctx ? entries : 0);
            register u64 x1 __asm__("x1") = ~0ULL;
            register u64 x2 __asm__("x2") = entry;
            register u64 x30 __asm__("x30") = (u64)g_saved_lr;
            __asm__ volatile("br x2" : : "r"(x0), "r"(x1), "r"(x2), "r"(x30) : "memory");
            __builtin_unreachable();
        }
    }
    say("\nDone. Press + to exit.\n");
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
