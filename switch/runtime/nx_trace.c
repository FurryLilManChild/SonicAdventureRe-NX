// SonicAdventureRe-nx: a trace of the pause, for the log. The link wraps the runner's pad hooks
// (native_hook_pad_scan, native_hook_pad_scanned; switch/build.sh's --wrap), so each of the game's
// pad scans passes here. While a stage is paused, and on the scans either side, a line is logged
// whenever what the pause reads changes: the stage's mode, the menu's choice and hiding, the
// first pad's held and newly pressed buttons, and the pictures that reached the screen since the
// pause began. A pause of the game's own makes a few lines; one that can't be left makes at most
// MOST_LINES.
#include <stdint.h>
#include <stdio.h>
#include <switch.h>

#define STAGE_PLAY 14
#define STAGE_PAUSE 15
#define MOST_LINES 400

// the game's own state (src/stage/stage.c, the game's mode)
extern short stage_mode;
extern short pause_choice;
extern short pause_hidden;
extern int game_mode;
extern int stage_current;
extern int act_current;
// pictures put on the screen so far (nx_overlay.c)
extern volatile unsigned long nx_overlay_presents;

struct native_pad {
    uint32_t id, support, on, off, press, release;
    uint16_t r, l;
    int16_t x1, y1, x2, y2;
};

void __real_native_hook_pad_scan(void *pad);
void __real_native_hook_pad_scanned(void);

static struct native_pad *record;
static unsigned long scans;
static int was_paused, lines;
static unsigned long presents_at_pause;
static short last_mode = -1, last_choice = -1, last_hidden = -1;
static uint32_t last_on = ~0u, last_press = ~0u;

void __wrap_native_hook_pad_scan(void *pad)
{
    record = pad;
    __real_native_hook_pad_scan(pad);
}

void __wrap_native_hook_pad_scanned(void)
{
    __real_native_hook_pad_scanned();
    scans++;
    const int paused = stage_mode == STAGE_PAUSE;
    if (!paused && !was_paused)
        return;
    if (paused && !was_paused) {
        presents_at_pause = nx_overlay_presents;
        fprintf(stderr, "pause: begins at scan %lu (game_mode %d, stage %d act %d)\n", scans, game_mode,
                stage_current, act_current);
    }
    uint32_t on = record ? record->on : 0, press = record ? record->press : 0;
    if (lines < MOST_LINES && (stage_mode != last_mode || pause_choice != last_choice ||
                               pause_hidden != last_hidden || on != last_on || press != last_press)) {
        lines++;
        fprintf(stderr,
                "pause: scan %lu: stage_mode %d, pause_choice %d, pause_hidden %d, pad held %#x pressed %#x, "
                "stick %d,%d; pictures shown since the pause %lu\n",
                scans, stage_mode, pause_choice, pause_hidden, on, press, record ? record->x1 : 0,
                record ? record->y1 : 0, nx_overlay_presents - presents_at_pause);
    }
    last_mode = stage_mode;
    last_choice = pause_choice;
    last_hidden = pause_hidden;
    last_on = on;
    last_press = press;
    if (!paused && was_paused)
        fprintf(stderr, "pause: ends at scan %lu, into stage_mode %d (game_mode %d, stage %d act %d), %lu "
                        "pictures shown during it\n",
                scans, stage_mode, game_mode, stage_current, act_current,
                nx_overlay_presents - presents_at_pause);
    was_paused = paused;
}
