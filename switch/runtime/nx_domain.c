// SonicAdventureRe-nx: the domain's maps on Horizon, for the decomp's runner.c and threads.c
// (patches/decomp/0003-horizon.patch). Proven by probe 2 on the owner's Switch: code memory from the
// heap at a chosen address, made RW (or RX for code), and views of it at other addresses through
// svcMapProcessMemory, which is how main RAM appears at 0x0c, 0x8c and 0xac000000.
//
// Every map is kept in a list and undone at exit (nx_domain_unmap_all): hbloader reuses the heap
// these maps alias once the program returns, which crashed probe 2's exit.
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#define MAX_MAPS 64

struct nx_map {
    u64 at, size, src; // a view has src 0 and view_of set
    u64 view_of;
};

static struct nx_map maps[MAX_MAPS];
static int map_count;
static u64 ram_at;

static Handle proc(void) { return envGetOwnProcessHandle(); }

static void note(const char *what, u64 at, u64 size, Result r)
{
    fprintf(stderr, "nx: %s at %#lx, %#lx bytes: %#x\n", what, at, size, r);
}

int nx_zeros(void *where, size_t size)
{
    u64 at = (u64)where;
    size = (size + 0xfff) & ~(size_t)0xfff;
    if (map_count >= MAX_MAPS || proc() == INVALID_HANDLE) {
        note("no room for a map", at, size, 0);
        return 0;
    }
    void *src = memalign(0x1000, size);
    if (!src) {
        note("no heap for a map", at, size, 0);
        return 0;
    }
    memset(src, 0, size);
    armDCacheFlush(src, size);
    Result r = svcMapProcessCodeMemory(proc(), at, (u64)src, size);
    if (R_SUCCEEDED(r))
        r = svcSetProcessMemoryPermission(proc(), at, size, Perm_Rw);
    if (R_FAILED(r)) {
        note("map failed", at, size, r);
        return 0;
    }
    maps[map_count++] = (struct nx_map){at, size, (u64)src, 0};
    return 1;
}

int nx_ram_view(void *where, size_t size)
{
    u64 at = (u64)where;
    if (!ram_at) {
        if (!nx_zeros(where, size))
            return 0;
        ram_at = at;
        return 1;
    }
    if (map_count >= MAX_MAPS)
        return 0;
    Result r = svcMapProcessMemory((void *)at, proc(), ram_at, size);
    if (R_FAILED(r)) {
        note("RAM view failed", at, size, r);
        return 0;
    }
    maps[map_count++] = (struct nx_map){at, size, 0, ram_at};
    return 1;
}

// code where the game wrote it: Horizon takes code memory once made writable as data for good
// (Switch Test 1: RW, then RX, failed 0xd401; probe 2: RX, then RW, worked). So the map is undone,
// which leaves what was written in the heap pages under it, and made again, executable first
int nx_code(void *where, size_t size)
{
    u64 at = (u64)where & ~0xfffULL;
    u64 end = ((u64)where + size + 0xfff) & ~0xfffULL;
    for (int i = 0; i < map_count; i++) {
        struct nx_map *m = &maps[i];
        if (m->view_of || at < m->at || end > m->at + m->size)
            continue;
        Result r = svcUnmapProcessCodeMemory(proc(), m->at, m->src, m->size);
        if (R_SUCCEEDED(r)) {
            armDCacheFlush((void *)m->src, m->size);
            r = svcMapProcessCodeMemory(proc(), m->at, m->src, m->size);
        }
        if (R_SUCCEEDED(r))
            r = svcSetProcessMemoryPermission(proc(), m->at, m->size, Perm_Rx);
        note("code mapped again, executable", m->at, m->size, r);
        if (R_FAILED(r))
            return 0;
        armICacheInvalidate((void *)m->at, m->size);
        return 1;
    }
    Result r = svcSetProcessMemoryPermission(proc(), at, end - at, Perm_Rx);
    if (R_FAILED(r)) {
        note("making code failed", at, end - at, r);
        return 0;
    }
    armICacheInvalidate((void *)at, end - at);
    return 1;
}

// a thread's stack anew: its pages are the threads' map already, so zeros alone
int nx_place(void *where, size_t size)
{
    memset(where, 0, size);
    return 1;
}

// a guard under a thread's stack. Horizon may refuse no-access code memory; the guard is a
// safety net only, so a refusal is noted once and the run goes on
int nx_guard(void *where, size_t size)
{
    static int told;
    Result r = svcSetProcessMemoryPermission(proc(), (u64)where, size, Perm_None);
    if (R_FAILED(r) && !told) {
        told = 1;
        note("stack guard refused (run goes on without guards)", (u64)where, size, r);
    }
    return 1;
}

// undo every map, views first: the program is about to return to hbloader
int nx_domain_unmap_all(void)
{
    int failed = 0;
    for (int i = map_count - 1; i >= 0; i--)
        if (maps[i].view_of) {
            Result r = svcUnmapProcessMemory((void *)maps[i].at, proc(), maps[i].view_of, maps[i].size);
            failed |= R_FAILED(r);
        }
    for (int i = map_count - 1; i >= 0; i--)
        if (!maps[i].view_of) {
            svcSetProcessMemoryPermission(proc(), maps[i].at, maps[i].size, Perm_Rw);
            Result r = svcUnmapProcessCodeMemory(proc(), maps[i].at, maps[i].src, maps[i].size);
            failed |= R_FAILED(r);
        }
    map_count = 0;
    ram_at = 0;
    return failed ? -1 : 0;
}

int nx_domain_maps(void) { return map_count; }
