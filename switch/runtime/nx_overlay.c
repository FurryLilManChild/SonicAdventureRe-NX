// SonicAdventureRe-nx: what the port draws over the game. The raster (patch 0006) calls
// native_overlay once for each picture it puts on the screen (and for each picture the port menu
// shows again), and copies what this gives over the window, after the game's picture (so the
// captures, read back before, never hold it).
//
// Two things: the frame counter at the top left (frames put on the screen a second, renewed
// every half second, logged as nx_overlay_fps), and the port menu (nx_menu.c), centered, while
// it is open.
#include <stdint.h>
#include <string.h>
#include <switch.h>

// a 5x7 font, a row a string, '#' set
struct glyph {
    char c;
    const char *rows[7];
};

static const struct glyph font[] = {
    {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
    {'C', {" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "}},
    {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
    {'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
    {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
    {'G', {" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"}},
    {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
    {'I', {" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'J', {"  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "}},
    {'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
    {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
    {'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
    {'N', {"#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"}},
    {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
    {'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
    {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
    {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
    {'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
    {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "}},
    {'X', {"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}},
    {'Y', {"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
    {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
    {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
    {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
    {'3', {"#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "}},
    {'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
    {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
    {'6', {"  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "}},
    {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
    {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
    {'9', {" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "}},
    {':', {"     ", "  #  ", "  #  ", "     ", "  #  ", "  #  ", "     "}},
    {'.', {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}},
    {'-', {"     ", "     ", "     ", "#####", "     ", "     ", "     "}},
    {'/', {"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}},
    {'%', {"##   ", "##  #", "   # ", "  #  ", " #   ", "#  ##", "   ##"}},
    {'>', {"#    ", " #   ", "  #  ", "   # ", "  #  ", " #   ", "#    "}},
    {'+', {"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}},
    {'(', {"   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "}},
    {')', {" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "}},
};

static const struct glyph *glyph_of(char c)
{
    for (size_t i = 0; i < sizeof font / sizeof font[0]; i++)
        if (font[i].c == c)
            return &font[i];
    return NULL; // a space, or anything else, draws nothing
}

// an RGBA canvas
struct canvas {
    uint8_t *px;
    int w, h;
};

static void fill(struct canvas *k, int x0, int y0, int w, int h, uint8_t v)
{
    for (int y = y0; y < y0 + h && y < k->h; y++)
        for (int x = x0; x < x0 + w && x < k->w; x++) {
            uint8_t *p = k->px + (y * k->w + x) * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
}

// text at (x, y), each font pixel scale by scale, gray level v; gives the width drawn
static int text(struct canvas *k, int x, int y, const char *s, int scale, uint8_t v)
{
    int at = x;
    for (; *s; s++, at += 6 * scale) {
        const struct glyph *g = glyph_of(*s);
        if (!g)
            continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g->rows[row][col] == '#')
                    fill(k, at + col * scale, y + row * scale, scale, scale, v);
    }
    return at - x;
}

// the frame counter
#define COUNTER_SCALE 3
#define COUNTER_W ((6 * 6 + 1) * COUNTER_SCALE + 4)
#define COUNTER_H (9 * COUNTER_SCALE + 4)
static uint8_t counter_px[COUNTER_W * COUNTER_H * 4];
static u64 window_start, frames;
static int shown_fps = -1;
volatile int nx_overlay_fps = -1;
// every picture put on the screen, for the pause trace (nx_trace.c)
volatile unsigned long nx_overlay_presents;
int nx_overlay_on = 1;

static void draw_counter(int fps)
{
    struct canvas k = {counter_px, COUNTER_W, COUNTER_H};
    fill(&k, 0, 0, k.w, k.h, 16);
    char s[8];
    if (fps < 0)
        memcpy(s, "-- FPS", 7);
    else {
        if (fps > 99)
            fps = 99;
        s[0] = fps >= 10 ? (char)('0' + fps / 10) : ' ';
        s[1] = (char)('0' + fps % 10);
        memcpy(s + 2, " FPS", 5);
    }
    text(&k, 2 + COUNTER_SCALE, 2 + COUNTER_SCALE, s, COUNTER_SCALE, 255);
}

// the menu (nx_menu.c)
extern volatile int nx_menu_open;
int nx_menu_lines(char lines[][40], int most, int *selected);
const char *nx_menu_hint(void);
#define MENU_SCALE 2
#define MENU_LINE (11 * MENU_SCALE)
#define MENU_W 440
#define MENU_H 300
static uint8_t menu_px[MENU_W * MENU_H * 4];

static void draw_menu(void)
{
    struct canvas k = {menu_px, MENU_W, MENU_H};
    char lines[16][40];
    int selected = -1;
    int n = nx_menu_lines(lines, 16, &selected);
    fill(&k, 0, 0, k.w, k.h, 230);       // a light frame
    fill(&k, 3, 3, k.w - 6, k.h - 6, 20); // a dark panel
    int y = 18;
    for (int i = 0; i < n; i++, y += MENU_LINE) {
        if (i == selected) {
            fill(&k, 12, y - 4, k.w - 24, 7 * MENU_SCALE + 8, 200);
            text(&k, 24, y, lines[i], MENU_SCALE, 10);
        } else {
            text(&k, 24, y, lines[i], MENU_SCALE, i == 0 ? 255 : 210);
        }
    }
    text(&k, 24, k.h - 14 - 7 * MENU_SCALE, nx_menu_hint(), MENU_SCALE, 150);
}

const uint8_t *native_overlay(uint32_t *width, uint32_t *height, int32_t *x, int32_t *y, uint32_t region_width,
                              uint32_t region_height)
{
    if (nx_menu_open) {
        // the counter starts afresh once the menu closes
        window_start = 0;
        frames = 0;
        draw_menu();
        *width = MENU_W;
        *height = MENU_H;
        *x = region_width > MENU_W ? (int32_t)(region_width - MENU_W) / 2 : 0;
        *y = region_height > MENU_H ? (int32_t)(region_height - MENU_H) / 2 : 0;
        return menu_px;
    }
    u64 now = armGetSystemTick(), freq = armGetSystemTickFreq();
    if (!window_start) {
        window_start = now;
        draw_counter(-1);
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
            draw_counter(fps);
        }
    }
    if (!nx_overlay_on)
        return NULL;
    *width = COUNTER_W;
    *height = COUNTER_H;
    *x = 8;
    *y = 8;
    return counter_px;
}
