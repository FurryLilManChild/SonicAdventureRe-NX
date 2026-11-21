// SonicAdventureRe-nx: the on-screen frame counter. The raster (patch 0006) calls native_overlay
// once for each picture it puts on the screen, and copies what this gives over the window's top
// left corner, after the picture (so the captures, read back before, never hold it).
//
// It counts those calls: the frames a player sees each second, not the frames the game finished.
// The figure is renewed every half second, and logged (nx_overlay_fps) in the heartbeat.
#include <stdint.h>
#include <string.h>
#include <switch.h>

#define SCALE 3
#define GLYPH_W 5
#define GLYPH_H 7
#define PAD 2
#define CHARS 6 // "60 FPS"
#define OW ((CHARS * (GLYPH_W + 1) + 1) * SCALE + 2 * PAD)
#define OH ((GLYPH_H + 2) * SCALE + 2 * PAD)

// 5x7 glyphs, a row a byte (bit 4 the left column): 0-9, space, F, P, S
static const uint8_t glyphs[14][GLYPH_H] = {
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e},
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
    {0, 0, 0, 0, 0, 0, 0},                      {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
};

static uint8_t pixels[OW * OH * 4];
static u64 window_start, frames;
static int shown_fps = -1;
volatile int nx_overlay_fps = -1;
// every picture put on the screen, for the pause trace (nx_trace.c)
volatile unsigned long nx_overlay_presents;
int nx_overlay_on = 1;

static void draw(int fps)
{
    // a dark box, white text
    for (int i = 0; i < OW * OH; i++) {
        pixels[i * 4 + 0] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 16;
        pixels[i * 4 + 3] = 255;
    }
    char text[CHARS + 1];
    if (fps < 0)
        memcpy(text, "-- FPS", CHARS);
    else {
        if (fps > 99)
            fps = 99;
        text[0] = fps >= 10 ? (char)('0' + fps / 10) : ' ';
        text[1] = (char)('0' + fps % 10);
        memcpy(text + 2, " FPS", 4);
    }
    for (int c = 0; c < CHARS; c++) {
        int g = text[c] >= '0' && text[c] <= '9' ? text[c] - '0'
                : text[c] == 'F'                 ? 11
                : text[c] == 'P'                 ? 12
                : text[c] == 'S'                 ? 13
                                                 : 10;
        for (int row = 0; row < GLYPH_H; row++)
            for (int col = 0; col < GLYPH_W; col++) {
                if (!(glyphs[g][row] & (0x10 >> col)))
                    continue;
                int x0 = PAD + (1 + c * (GLYPH_W + 1) + col) * SCALE, y0 = PAD + (1 + row) * SCALE;
                for (int dy = 0; dy < SCALE; dy++)
                    for (int dx = 0; dx < SCALE; dx++) {
                        uint8_t *p = pixels + ((y0 + dy) * OW + x0 + dx) * 4;
                        p[0] = p[1] = p[2] = 255;
                    }
            }
    }
}

const uint8_t *native_overlay(uint32_t *width, uint32_t *height, int32_t *x, int32_t *y)
{
    u64 now = armGetSystemTick(), freq = armGetSystemTickFreq();
    if (!window_start) {
        window_start = now;
        draw(-1);
    }
    frames++;
    nx_overlay_presents++;
    if (now - window_start >= freq / 2) {
        int fps = (int)((frames * freq + (now - window_start) / 2) / (now - window_start));
        frames = 0;
        window_start = now;
        nx_overlay_fps = fps;
        if (fps != shown_fps) {
            shown_fps = fps;
            draw(fps);
        }
    }
    if (!nx_overlay_on)
        return NULL;
    *width = OW;
    *height = OH;
    *x = 8;
    *y = 8;
    return pixels;
}
