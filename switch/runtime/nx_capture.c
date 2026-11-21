// SonicAdventureRe-nx: captures for Claude, saved to sdmc:/switch/SonicAdventureRe-nx/logs/.
//
//   Right stick click: the picture on screen, as shot_NNN.bmp.
//   Left stick click: the last 120 pictures shown (2 seconds at 60 fps, 4 at 30), as
//     replay_NNN.bin, at most 10 a session. tools/replay/replay.py turns one into PNGs.
//
// play.c (patch 0004) hands each picture shown to native_play_capture, 640x480 RGBA read back
// from the GPU, which turns the engine's readback on. The ring keeps them as RGB (110 MB, taken
// once). Saving runs on the game's thread, so the game stops while a replay is written.
//
// replay_NNN.bin: a header, then per picture a record, then the pictures:
//   header  "SARNXRP1", u32 count, u32 width, u32 height, u32 wide (1 where the aspect is 16:9,
//           so the picture is shown stretched to 16:9), char build[16]
//   record  u32 picture, u32 vsync, s32 game_mode, s32 stage, s32 act, u32 reserved
//   picture width*height*3 bytes, RGB, top row first
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#define DIR "sdmc:/switch/SonicAdventureRe-nx"
#define LOGS DIR "/logs"
#define W 640
#define H 480
#define RING 120
#define MOST_REPLAYS 10

#ifndef NX_BUILD
#define NX_BUILD "unknown"
#endif

// the game's own state, as shim.c reports it
extern int game_mode;
extern int stage_current;
extern int act_current;

// set by the window (window_nx.cpp) on a stick click: 1 a picture, 2 a replay
volatile int nx_capture_request;
// 1 where the settings ask for 16:9 (nx_main.c reads them)
int nx_capture_wide;

struct record {
    uint32_t picture, vsync;
    int32_t game_mode, stage, act;
    uint32_t reserved;
};

static uint8_t *ring;
static struct record records[RING];
static uint32_t filled, next;
static int replays, ring_failed;
static u64 copy_ticks, copies;

static double ms(u64 ticks) { return ticks * 1000.0 / armGetSystemTickFreq(); }

static FILE *numbered(const char *stem, const char *ext, char *path, size_t size, unsigned *number)
{
    mkdir(LOGS, 0777);
    for (unsigned n = 1; n < 1000; n++) {
        snprintf(path, size, LOGS "/%s_%03u.%s", stem, n, ext);
        FILE *t = fopen(path, "rb");
        if (t) {
            fclose(t);
            continue;
        }
        *number = n;
        return fopen(path, "wb");
    }
    return NULL;
}

static void put32(uint8_t *at, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        at[i] = (uint8_t)(v >> (8 * i));
}

static void save_shot(const uint8_t *rgba, uint32_t picture, uint32_t vsync)
{
    char path[128];
    unsigned n = 0;
    u64 t0 = armGetSystemTick();
    FILE *f = numbered("shot", "bmp", path, sizeof path, &n);
    if (!f) {
        fprintf(stderr, "nx: capture: can't make a picture file in %s\n", LOGS);
        return;
    }
    // shown at 16:9 the picture is stretched, so the file is too: 854 wide, nearest column
    const int ow = nx_capture_wide ? 854 : W;
    const uint32_t row_bytes = (uint32_t)ow * 3, pad = (4 - row_bytes % 4) % 4;
    const uint32_t img = (row_bytes + pad) * H;
    uint8_t hdr[54] = {'B', 'M'};
    put32(hdr + 2, 54 + img);
    put32(hdr + 10, 54);
    put32(hdr + 14, 40);
    put32(hdr + 18, (uint32_t)ow);
    put32(hdr + 22, (uint32_t)-H); // top row first
    hdr[26] = 1;
    hdr[28] = 24;
    put32(hdr + 34, img);
    fwrite(hdr, 1, sizeof hdr, f);
    uint8_t *row = malloc(row_bytes + pad);
    if (row) {
        memset(row, 0, row_bytes + pad);
        for (int y = 0; y < H; y++) {
            const uint8_t *src = rgba + (size_t)y * W * 4;
            for (int x = 0; x < ow; x++) {
                const uint8_t *p = src + (size_t)(x * W / ow) * 4;
                row[x * 3 + 0] = p[2];
                row[x * 3 + 1] = p[1];
                row[x * 3 + 2] = p[0];
            }
            fwrite(row, 1, row_bytes + pad, f);
        }
        free(row);
    }
    int ok = !ferror(f);
    ok = !fclose(f) && ok;
    fprintf(stderr, "nx: capture: picture %u (vsync %u, game_mode %d, stage %d act %d) %s %s, %.0f ms\n",
            picture, vsync, game_mode, stage_current, act_current, ok ? "saved as" : "FAILED writing", path,
            ms(armGetSystemTick() - t0));
}

static void save_replay(void)
{
    if (replays >= MOST_REPLAYS) {
        fprintf(stderr, "nx: capture: replay refused, %d saved this session already (the most)\n", replays);
        return;
    }
    if (!filled) {
        fprintf(stderr, "nx: capture: replay refused, no pictures kept yet\n");
        return;
    }
    char path[128];
    unsigned n = 0;
    u64 t0 = armGetSystemTick();
    FILE *f = numbered("replay", "bin", path, sizeof path, &n);
    if (!f) {
        fprintf(stderr, "nx: capture: can't make a replay file in %s\n", LOGS);
        return;
    }
    const uint32_t first = filled < RING ? 0 : next;
    uint8_t hdr[8 + 16 + 16] = {0};
    memcpy(hdr, "SARNXRP1", 8);
    put32(hdr + 8, filled);
    put32(hdr + 12, W);
    put32(hdr + 16, H);
    put32(hdr + 20, (uint32_t)nx_capture_wide);
    strncpy((char *)hdr + 24, NX_BUILD, 15);
    fwrite(hdr, 1, sizeof hdr, f);
    for (uint32_t i = 0; i < filled; i++)
        fwrite(&records[(first + i) % RING], sizeof records[0], 1, f);
    for (uint32_t i = 0; i < filled; i++)
        fwrite(ring + (size_t)((first + i) % RING) * W * H * 3, 1, (size_t)W * H * 3, f);
    int ok = !ferror(f);
    ok = !fclose(f) && ok;
    replays++;
    const struct record *a = &records[first % RING], *b = &records[(first + filled - 1) % RING];
    fprintf(stderr, "nx: capture: replay of %u pictures (%u to %u, vsyncs %u to %u) %s %s, %.1f s\n", filled,
            a->picture, b->picture, a->vsync, b->vsync, ok ? "saved as" : "FAILED writing", path,
            ms(armGetSystemTick() - t0) / 1000.0);
}

void native_play_capture(const uint8_t *rgba, uint32_t picture, uint32_t tick)
{
    if (!ring && !ring_failed) {
        ring = malloc((size_t)RING * W * H * 3);
        ring_failed = !ring;
        fprintf(stderr, "nx: capture: %s (%u pictures, %u MB); right stick click saves a picture, left stick "
                        "click the last %u\n",
                ring ? "replay ring ready" : "NO MEMORY for the replay ring, pictures only", RING,
                (unsigned)((size_t)RING * W * H * 3 >> 20), RING);
    }
    if (ring) {
        u64 t0 = armGetSystemTick();
        uint8_t *dst = ring + (size_t)next * W * H * 3;
        for (size_t i = 0; i < (size_t)W * H; i++) {
            dst[i * 3 + 0] = rgba[i * 4 + 0];
            dst[i * 3 + 1] = rgba[i * 4 + 1];
            dst[i * 3 + 2] = rgba[i * 4 + 2];
        }
        records[next] = (struct record){picture, tick, game_mode, stage_current, act_current, 0};
        next = (next + 1) % RING;
        if (filled < RING)
            filled++;
        copy_ticks += armGetSystemTick() - t0;
        copies++;
    }
    int want = nx_capture_request;
    if (want) {
        nx_capture_request = 0;
        if (want & 1)
            save_shot(rgba, picture, tick);
        if (want & 2)
            save_replay();
    }
}

// for the heartbeat: the ring's copying, in ms a picture on average since the last call
double nx_capture_copy_ms(void)
{
    double r = copies ? ms(copy_ticks) / copies : 0.0;
    copy_ticks = 0;
    copies = 0;
    return r;
}
