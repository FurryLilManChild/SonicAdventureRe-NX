// SonicAdventureRe-nx: the NRO, a loader. The game is a program image linked for a fixed 4 GB
// window (the domain, where its 32-bit pointers point), and Horizon places its heap and alias
// regions in different windows on each launch. So this picks a window clear of them, places the
// image there from sdmc:/switch/SonicAdventureRe-nx/SonicAdventureRe-nx.img (switch/tools/
// mkimage.py: segments and the 64-bit addresses to move), and starts it with the launcher's
// entries, its heap less the part the image's pages came from. The game returns here when it
// ends (its return address is loader_return), and the image is unmapped before hbloader takes
// its heap back. Proven piece by piece by probes 1 and 2 on the owner's Switch.
//
// The image the port ships holds none of the disc's data (switch/tools/strip.py): its flags are 1,
// and SonicAdventureRe-nx.fill says where each zeroed byte comes from on the player's Japanese
// disc, in disc/. The first launch fills it from there (each file checked by its SHA-1) and saves
// the whole image as SonicAdventureRe-nx.built.img (flags 2, the same header otherwise), which
// later launches load as it is.
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "english_build.h"

#define DIR "sdmc:/switch/SonicAdventureRe-nx"
#define CARVE (256ULL << 20) // the heap's end, kept for the image's pages

void __appExit(void);

static FILE *logf;
static void *g_ctx;
static LoaderReturnFn g_return;
static u64 g_carve, g_carve_size;
static u64 g_image_at, g_image_size;

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

void __real_envSetup(void *ctx, Handle main_thread, LoaderReturnFn saved_lr);
void __wrap_envSetup(void *ctx, Handle main_thread, LoaderReturnFn saved_lr)
{
    g_ctx = ctx;
    g_return = saved_lr;
    __real_envSetup(ctx, main_thread, saved_lr);
}

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

// the game returns here (its exit jumps to the return address it started with): its image's
// pages go back to the heap, then on to hbloader. Services are closed by now: kernel calls alone
static void __attribute__((noreturn)) loader_return(int rc)
{
    Handle proc = envGetOwnProcessHandle();
    if (g_image_size) {
        svcSetProcessMemoryPermission(proc, g_image_at, g_image_size, Perm_Rw);
        svcUnmapProcessCodeMemory(proc, g_image_at, g_carve, g_image_size);
    }
    g_return(rc);
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

// every range the game maps in its window (engine/native/runner.c, threads.c, shim.c), offsets
static u64 ranges[][2] = {
    {0x00400000, 0},          // the image, its size filled in
    {0x0c000000, 0x01000000}, {0x8c000000, 0x01000000}, {0xac000000, 0x01000000}, // RAM, mirrors
    {0xa0000000, 0x01000000}, {0xa4000000, 0x02000000}, {0x10000000, 0x02000000},
    {0x7c000000, 0x00010000}, {0xe0000000, 0x04000000}, {0xff000000, 0x01000000}, // devices
    {0x40000000, 0x01000000},                                                       // the game's stack
    {0x42000000, 0x01450000},                                                       // threads' stacks
    {0x50000000, 0x10000000},                                                       // Shinobi's heap
    {0xb0000000, 0x00001000},                                                       // the P2 calls
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

// --- the first launch's build from the player's disc ---

struct fill_header {
    char magic[8];
    u32 sources, pieces, crc, pad;
};
struct fill_source {
    char path[32];
    u32 size;
    u8 sha1[20];
    u32 prs, pad;
};
struct fill_piece {
    u32 at, source, from, length;
};

// Sega's PRS: an LZ77 with flag bits read low bit first; gives the bytes written, or 0 if src ends
// early or out would overflow
static u64 prs_decode(const u8 *src, u64 src_size, u8 *out, u64 room)
{
    u64 in = 0, o = 0;
    u32 flags = 0, left = 0;
#define PRS_BIT()                                                                                                  \
    ({                                                                                                         \
        if (!left) {                                                                                           \
            if (in >= src_size)                                                                                \
                return 0;                                                                                      \
            flags = src[in++];                                                                                 \
            left = 8;                                                                                          \
        }                                                                                                      \
        u32 b_ = flags & 1;                                                                                    \
        flags >>= 1;                                                                                           \
        left--;                                                                                                \
        b_;                                                                                                    \
    })
    for (;;) {
        if (PRS_BIT()) {
            if (in >= src_size || o >= room)
                return 0;
            out[o++] = src[in++];
            continue;
        }
        long offset;
        u64 size;
        if (PRS_BIT()) {
            if (in + 1 >= src_size)
                return 0;
            u32 word = src[in] | src[in + 1] << 8;
            in += 2;
            if (!word)
                break;
            offset = (long)(word >> 3) - 0x2000;
            size = word & 7;
            if (size)
                size += 2;
            else {
                if (in >= src_size)
                    return 0;
                size = (u64)src[in++] + 1;
            }
        } else {
            u32 hi = PRS_BIT();
            u32 lo = PRS_BIT();
            size = (hi << 1 | lo) + 2;
            if (in >= src_size)
                return 0;
            offset = (long)src[in++] - 0x100;
        }
        if ((long)o + offset < 0 || o + size > room)
            return 0;
        for (u64 i = 0; i < size; i++, o++)
            out[o] = out[o + offset];
    }
#undef PRS_BIT
    return o;
}

static u8 *read_file(const char *path, u64 *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (u64)n;
    return buf;
}

static int by_source(const void *a, const void *b)
{
    const struct fill_piece *x = a, *y = b;
    return x->source != y->source ? (x->source < y->source ? -1 : 1) : (x->at < y->at ? -1 : x->at > y->at);
}

// fills the image (dst, size bytes) from the disc; false with the reason said
static bool fill_from_disc(u8 *dst, u64 size, u32 crc)
{
    u64 n = 0;
    u8 *table = read_file(DIR "/SonicAdventureRe-nx.fill", &n);
    struct fill_header *fh = (struct fill_header *)table;
    if (!table || n < sizeof *fh || memcmp(fh->magic, "SARNXFL1", 8) ||
        n < sizeof *fh + fh->sources * sizeof(struct fill_source) + fh->pieces * (u64)sizeof(struct fill_piece)) {
        say("can't read %s/SonicAdventureRe-nx.fill: copy it beside the .nro\n", DIR);
        free(table);
        return false;
    }
    if (fh->crc != crc) {
        say("SonicAdventureRe-nx.fill is from another build than SonicAdventureRe-nx.img: copy both again\n");
        free(table);
        return false;
    }
    struct fill_source *src = (struct fill_source *)(fh + 1);
    struct fill_piece *pc = (struct fill_piece *)(src + fh->sources);
    qsort(pc, fh->pieces, sizeof *pc, by_source);
    say("\nFirst launch: building the game from your Japanese disc (%u files). This takes a minute,\n"
        "once; later launches start at once.\n\n", fh->sources);
    u32 k = 0;
    for (u32 s = 0; s < fh->sources; s++) {
        char path[160];
        snprintf(path, sizeof path, DIR "/disc/%s", src[s].path);
        u64 raw_size = 0;
        u8 *raw = read_file(path, &raw_size);
        if (!raw) {
            say("\nMissing from your disc folder: %s\n(the Japanese disc's files go in %s/disc/)\n", src[s].path, DIR);
            free(table);
            return false;
        }
        u8 *data = raw;
        if (src[s].prs) {
            data = malloc(src[s].size + 16);
            if (!data || prs_decode(raw, raw_size, data, src[s].size) != src[s].size) {
                say("\n%s doesn't decode as this port expects\n", src[s].path);
                free(raw);
                free(data);
                free(table);
                return false;
            }
            free(raw);
        } else if (raw_size != src[s].size) {
            say("\n%s is %lu bytes, not the %u of the Japanese disc (HDR-0001, v1.007)\n", src[s].path, raw_size,
                src[s].size);
            free(raw);
            free(table);
            return false;
        }
        u8 sum[20];
        sha1CalculateHash(sum, data, src[s].size);
        if (memcmp(sum, src[s].sha1, 20)) {
            say("\n%s isn't the Japanese disc's (HDR-0001, v1.007): this port needs that disc\n", src[s].path);
            free(data);
            free(table);
            return false;
        }
        for (; k < fh->pieces && pc[k].source == s; k++) {
            if ((u64)pc[k].at + pc[k].length > size || (u64)pc[k].from + pc[k].length > src[s].size) {
                say("\nthe fill table points past %s\n", src[s].path);
                free(data);
                free(table);
                return false;
            }
            memcpy(dst + pc[k].at, data + pc[k].from, pc[k].length);
        }
        free(data);
        if (s % 16 == 15 || s + 1 == fh->sources)
            say("  %u of %u files\n", s + 1, fh->sources);
    }
    free(table);
    return true;
}

static bool read_all(FILE *f, void *to, u64 size)
{
    u8 *p = to;
    while (size) {
        size_t n = fread(p, 1, size > (1 << 20) ? (1 << 20) : size, f);
        if (!n)
            return false;
        p += n;
        size -= n;
    }
    return true;
}

static bool place(FILE *f, const struct image_header *h, const struct image_segment *seg, u64 base)
{
    Handle proc = envGetOwnProcessHandle();
    u64 size = h->end - h->start, at = base + (h->start - h->link_base), delta = base - h->link_base;
    u8 *dst = (u8 *)g_carve;
    say("image: %lu MB, linked for %#lx, going to %#lx, %u addresses to move\n", size >> 20, h->link_base, at,
        h->relocations);
    memset(dst, 0, size);
    for (u32 i = 0; i < h->segments; i++) {
        if (!read_all(f, dst + seg[i].offset, seg[i].filesz)) {
            say("the image file ends early (segment %u)\n", i);
            return false;
        }
        u64 pad = (8 - seg[i].filesz % 8) % 8;
        if (pad)
            fseek(f, pad, SEEK_CUR);
    }
    u32 *rel = malloc(h->relocations * 4ULL + 4);
    if (!rel || !read_all(f, rel, h->relocations * 4ULL)) {
        say("can't read the image's address list\n");
        return false;
    }
    if (h->flags & 1) {
        if (!fill_from_disc(dst, size, h->pad))
            return false;
        // saved whole, for the next launch: the header (flags 2), segments and addresses
        say("saving the built game for later launches...\n");
        FILE *b = fopen(DIR "/SonicAdventureRe-nx.built.tmp", "wb");
        struct image_header bh = *h;
        bh.flags = 2;
        bool ok = b && fwrite(&bh, sizeof bh, 1, b) == 1 && fwrite(seg, sizeof seg[0], h->segments, b) == h->segments;
        for (u32 i = 0; ok && i < h->segments; i++) {
            static const u8 zeros[8];
            u64 pad = (8 - seg[i].filesz % 8) % 8;
            ok = fwrite(dst + seg[i].offset, 1, seg[i].filesz, b) == seg[i].filesz && fwrite(zeros, 1, pad, b) == pad;
        }
        ok = ok && fwrite(rel, 4, h->relocations, b) == h->relocations;
        ok = b && !fclose(b) && ok;
        if (ok) {
            remove(DIR "/SonicAdventureRe-nx.built.img");
            ok = !rename(DIR "/SonicAdventureRe-nx.built.tmp", DIR "/SonicAdventureRe-nx.built.img");
        }
        say(ok ? "saved\n" : "couldn't save it (the game still starts; the next launch builds again)\n");
    }
    for (u32 i = 0; i < h->relocations; i++)
        *(u64 *)(dst + rel[i]) += delta;
    free(rel);
    armDCacheFlush(dst, size);
    Result r = svcMapProcessCodeMemory(proc, at, (u64)dst, size);
    say("mapped at %#lx: %#x\n", at, r);
    if (R_FAILED(r))
        return false;
    g_image_at = at;
    g_image_size = size;
    for (u32 i = 0; i < h->segments; i++) {
        u64 s = at + (seg[i].offset & ~0xfffULL);
        u64 e = (at + seg[i].offset + seg[i].memsz + 0xfff) & ~0xfffULL;
        u32 perm = (seg[i].flags & 1) ? Perm_Rx : (seg[i].flags & 2) ? Perm_Rw : Perm_R;
        Result rp = svcSetProcessMemoryPermission(proc, s, e - s, perm);
        if (R_FAILED(rp)) {
            say("segment %u at %#lx, permission %u: %#x\n", i, s, perm, rp);
            return false;
        }
    }
    armICacheInvalidate((void *)at, size);
    return true;
}

static void wait_for_plus(void)
{
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    mkdir(DIR, 0777);
    logf = fopen(DIR "/SonicAdventureRe-nx.log", "a");
    say("\n== SonicAdventureRe-nx loader (build %s) ==\n", NX_BUILD);
    if (envGetOwnProcessHandle() == INVALID_HANDLE) {
        say("Launch through title takeover (hold R while opening a game), not the Album.\n");
        goto fail;
    }
    FILE *f = fopen(DIR "/SonicAdventureRe-nx.img", "rb");
    struct image_header h;
    static struct image_segment seg[8];
    if (!f || fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "SAREIMG1", 8) || h.segments > 8 ||
        fread(seg, sizeof seg[0], h.segments, f) != h.segments) {
        say("can't read %s/SonicAdventureRe-nx.img\n", DIR);
        goto fail;
    }
    if (h.flags & 1) {
        // the built image, where it is this image's
        FILE *b = fopen(DIR "/SonicAdventureRe-nx.built.img", "rb");
        struct image_header bh;
        static struct image_segment bseg[8];
        if (b && fread(&bh, sizeof bh, 1, b) == 1 && bh.flags == 2 && bh.segments == h.segments &&
            fread(bseg, sizeof bseg[0], bh.segments, b) == bh.segments && !memcmp(bseg, seg, sizeof seg[0] * h.segments)) {
            struct image_header want = h;
            want.flags = 2;
            if (!memcmp(&want, &bh, sizeof bh)) {
                say("the game built from your disc: SonicAdventureRe-nx.built.img\n");
                fclose(f);
                f = b;
                h = bh;
                b = NULL;
            }
        }
        if (b)
            fclose(b);
    }
    // the English layer, made once from the US disc's files where the player put them
    {
        struct stat st;
        if (!stat(DIR "/us_disc", &st) && S_ISDIR(st.st_mode) && !english_current(DIR "/english")) {
            mkdir(DIR "/english", 0777);
            if (english_build(DIR "/us_disc", DIR "/disc", DIR "/english", say))
                say("the English layer could not be made: the game starts in Japanese\n");
        }
    }
    if (h.end - h.start > g_carve_size) {
        say("the image (%lu MB) is bigger than the heap kept for it (%lu MB)\n", (h.end - h.start) >> 20,
            g_carve_size >> 20);
        goto fail;
    }
    ranges[0][1] = (h.end - h.start + 0xfff) & ~0xfffULL;
    u64 chosen = 0;
    char list[128] = "";
    for (u64 w = 1; w < 16; w++)
        if (window_ok(w)) {
            char item[8];
            snprintf(item, sizeof item, "%lu ", w);
            strncat(list, item, sizeof list - strlen(list) - 1);
            if (!chosen)
                chosen = w;
        }
    say("usable windows: %s\nchosen: %lu\n", list, chosen);
    if (!chosen) {
        say("no 4 GB window is free of Horizon's regions this launch: relaunch\n");
        goto fail;
    }
    if (!place(f, &h, seg, chosen << 32))
        goto fail;
    fclose(f);

    static ConfigEntry entries[64];
    const ConfigEntry *e = g_ctx;
    int n = 0;
    for (; e && e->Key != EntryType_EndOfList && n < 63; e++, n++) {
        entries[n] = *e;
        if (e->Key == EntryType_OverrideHeap)
            entries[n].Value[1] = g_carve - e->Value[0];
    }
    entries[n].Key = EntryType_EndOfList;
    u64 entry = (chosen << 32) + (h.entry - h.link_base);
    say("starting the game at %#lx\n", entry);
    if (logf)
        fclose(logf);
    consoleExit(NULL);
    __appExit();
    register u64 x0 __asm__("x0") = (u64)(g_ctx ? entries : 0);
    register u64 x1 __asm__("x1") = ~0ULL;
    register u64 x2 __asm__("x2") = entry;
    register u64 x30 __asm__("x30") = (u64)loader_return;
    __asm__ volatile("br x2" : : "r"(x0), "r"(x1), "r"(x2), "r"(x30) : "memory");
    __builtin_unreachable();

fail:
    say("\nPress + to exit.\n");
    wait_for_plus();
    if (logf)
        fclose(logf);
    consoleExit(NULL);
    return 0;
}
