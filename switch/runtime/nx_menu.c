// SonicAdventureRe-nx: the port menu. Minus and + together open it over the game, which waits
// (the backend holds it in its pad read, patch 0006, and shows the last picture again each vsync
// with this menu over it). Sound goes quiet while it shows.
//
//   up and down choose, left and right (or A) change, B or + resumes.
//
// Widescreen, 60 fps and resolution change at once (the decomp's own port toggles, and the
// raster's rescale, patch 0006); captures take effect at the next start. Every change is written
// to settings.txt.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#define DIR "sdmc:/switch/SonicAdventureRe-nx"
#define SETTINGS DIR "/settings.txt"

// the window's buttons (window_nx.cpp): it updates the pad and gives the newly pressed and held
// buttons, and whether the system asks the program to end
int nx_window_buttons(u64 *down, u64 *held);
// the decomp's port toggles (src/system/port.h): 1 is 16:9, 2 is 60 fps
extern int port_toggles[3];
extern int port_chosen[3];
// the frame counter (nx_overlay.c)
extern int nx_overlay_on;
extern int nx_capture_enabled;
extern int nx_capture_wide;
// the English layer (nx_english.c)
extern int nx_voices_english, nx_text_english;
int nx_english_has_voices(void);
int nx_english_has_text(void);

volatile int nx_menu_open;     // read by the sound thread: quiet while 1
volatile int nx_menu_request;  // set by the window on Minus and +
volatile int nx_menu_quit;     // set by Quit: the window then ends the game
int nx_volume = 100;           // percent, read by the sound thread
int nx_resolution = 1;         // settings.txt's resolution (the decomp's, kept at 1)
int nx_halves = 2;             // the port's resolution in halves of 640x480: 2, 3 or 4 (1X to 2X)
int nx_settings_capture;       // the captures setting for the next start
int nx_trials_open;            // 1: every Trial stage and mini game open (patch 0019), the save untouched
int nx_translucency_fast = 2;  // 0: per pixel, as the Dreamcast sorts it; 1: per triangle (faster);
                               // 2: per pixel, but sprites and polygons (smoke, dust) per triangle
volatile int nx_talk_request;  // set by the window when TALK is pressed, taken by the game's step
int nx_dpad_move;              // 0: the D-pad is its own, as on the Dreamcast (first-person look); 1: it walks
int nx_camera_stick = 1;       // the right stick turns the camera (L and R): 0 off, 1 on, 2 inverted
int nx_idle_lines = 1;         // 1: a character standing still says its idle line as it fidgets, as in SADX

// --- the controls: which Switch button presses each Dreamcast button, and TALK ---
// (window_nx.cpp reads them each pad read; settings.txt keeps them as button_a b ... talk minus)
enum { CTL_A, CTL_B, CTL_X, CTL_Y, CTL_L, CTL_R, CTL_TALK, CTLS };
static const struct {
    const char *name, *setting;
    u64 mask;
} sw_buttons[] = {
    {"A", "a", HidNpadButton_A},
    {"B", "b", HidNpadButton_B},
    {"X", "x", HidNpadButton_X},
    {"Y", "y", HidNpadButton_Y},
    {"L", "l", HidNpadButton_L},
    {"R", "r", HidNpadButton_R},
    {"ZL", "zl", HidNpadButton_ZL},
    {"ZR", "zr", HidNpadButton_ZR},
    {"L + ZL", "l+zl", HidNpadButton_L | HidNpadButton_ZL},
    {"R + ZR", "r+zr", HidNpadButton_R | HidNpadButton_ZR},
    {"MINUS TAP", "minus", HidNpadButton_Minus}, // TALK only
    {"NONE", "none", 0},                         // TALK only
};
#define SW_DC_CHOICES 10 // a Dreamcast button takes A to R + ZR
#define SW_TALK_CHOICES 12
static const char *const ctl_names[CTLS] = {"DREAMCAST A", "DREAMCAST B", "DREAMCAST X", "DREAMCAST Y",
                                            "L TRIGGER",   "R TRIGGER",   "TALK"};
static const char *const ctl_settings[CTLS] = {"button_a", "button_b", "button_x", "button_y",
                                               "button_l", "button_r", "talk"};
// the usual layout: B jumps (Dreamcast A), A acts (B), Y is X, X is Y, the shoulders turn the camera
static const int ctl_defaults[CTLS] = {1, 0, 3, 2, 8, 9, 10};
static int ctl_map[CTLS] = {1, 0, 3, 2, 8, 9, 10};

// the Switch buttons pressing Dreamcast button i (CTL_A to CTL_R), or TALK's (Minus: a tap alone)
u64 nx_control_mask(int i)
{
    return i >= 0 && i < CTLS ? sw_buttons[ctl_map[i]].mask : 0;
}

// a settings.txt line (nx_main.c): 1 where it was a control's
int nx_control_setting(const char *name, const char *value)
{
    for (int i = 0; i < CTLS; i++)
        if (!strcmp(name, ctl_settings[i])) {
            int most = i == CTL_TALK ? SW_TALK_CHOICES : SW_DC_CHOICES;
            for (int k = 0; k < most; k++)
                if (!strcmp(value, sw_buttons[k].setting))
                    ctl_map[i] = k;
            return 1;
        }
    return 0;
}

// the controls as a log line
void nx_controls_describe(char *out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    for (int i = 0; i < CTLS && n < size; i++)
        n += snprintf(out + n, size - n, "%s%s %s", i ? ", " : "", ctl_settings[i], sw_buttons[ctl_map[i]].setting);
}

// the game asks each step whether TALK was pressed, and whether idle lines play (patch 0017): the
// characters' idle lines, from SADX's table (tools/idle)
int __wrap_native_hook_talk(void)
{
    if (!nx_talk_request)
        return 0;
    nx_talk_request = 0;
    return 1;
}

int __wrap_native_hook_trials_open(void)
{
    return nx_trials_open;
}

// SAVE NOW (patch 0026): each frame an adventure field plays in a story the game says whether it
// may save there; the menu's request is taken on the next such frame, and the result shows over
// the game for a few seconds (nx_overlay.c)
static volatile u64 field_tick, save_request_tick;
static volatile int field_can, save_request;
volatile int nx_save_result; // 1 saved, -1 the card refused it
volatile u64 nx_save_until;  // the system tick the result shows until

static int can_save_now(void)
{
    return field_can && field_tick && armGetSystemTick() - field_tick < armGetSystemTickFreq() / 4;
}

int __wrap_native_hook_save_now(int can)
{
    u64 now = armGetSystemTick();
    field_tick = now;
    field_can = can;
    if (!save_request || !can)
        return 0;
    save_request = 0;
    if (now - save_request_tick > armGetSystemTickFreq())
        return 0;
    return 1;
}

void __wrap_native_hook_save_done(int result)
{
    if (result == 0)
        fprintf(stderr, "menu: save now: saved\n");
    else
        fprintf(stderr, "menu: save now: the card refused it (%d)\n", result);
    nx_save_result = result == 0 ? 1 : -1;
    nx_save_until = armGetSystemTick() + 3 * armGetSystemTickFreq();
}

int __wrap_native_hook_idle_lines(void)
{
    return nx_idle_lines;
}

// the game says which idle line it plays (patch 0017), for the log
void __wrap_native_hook_idle_said(int voice, int fidget, int stage, int act, int character)
{
    fprintf(stderr, "idle line: voice %d (%s), stage %d act %d, character %d\n", voice, fidget ? "standing still" : "TALK",
            stage, act, character);
}

// the renderer asks each picture how its translucent triangles compose (patch 0016):
// SA_PC_BLEND_PIXEL (0) or SA_PC_BLEND_TRIANGLE (3)
int native_blend_mode(void)
{
    return nx_translucency_fast == 1 ? 3 : nx_translucency_fast == 2 ? 4 : 0;
}

// the renderer's device time for each picture in parts (patch 0021): the pass, the resolve (the
// per-pixel sort, ACCURATE and EFFECTS FAST only) and the rest (the late effects, the copies),
// and how many translucent fragments the pass kept for the resolve. Every 120 pictures the log
// gets one line of averages and peaks, with the TRANSLUCENCY setting they were drawn under
static uint32_t gpu_count, gpu_mode;
static uint64_t gpu_pass, gpu_resolve, gpu_rest, gpu_total, gpu_most, gpu_frags;
static uint32_t gpu_most_frags;

static void gpu_line(void)
{
    uint32_t n = gpu_count;

    fprintf(stderr,
            "[gpu] %s: %u pictures: device %.2f ms a picture (most %.2f): pass %.2f, resolve %.2f, "
            "rest %.2f; translucent layers %.0f a picture (most %u)\n",
            gpu_mode == 1 ? "fast" : gpu_mode == 2 ? "effects" : "accurate", n, gpu_total / 1e6 / n,
            gpu_most / 1e6, gpu_pass / 1e6 / n, gpu_resolve / 1e6 / n, gpu_rest / 1e6 / n,
            (double)gpu_frags / n, gpu_most_frags);
    gpu_count = 0;
    gpu_pass = gpu_resolve = gpu_rest = gpu_total = gpu_most = gpu_frags = 0;
    gpu_most_frags = 0;
}

void native_gpu_parts(uint64_t pass_ns, uint64_t resolve_ns, uint64_t rest_ns, uint32_t fragments)
{
    uint32_t now = (uint32_t)nx_translucency_fast;
    uint64_t total = pass_ns + resolve_ns + rest_ns;

    // a change of setting ends the stretch early, so no line mixes two settings
    if (gpu_count != 0 && now != gpu_mode)
        gpu_line();
    gpu_mode = now;
    gpu_count++;
    gpu_pass += pass_ns;
    gpu_resolve += resolve_ns;
    gpu_rest += rest_ns;
    gpu_total += total;
    gpu_frags += fragments;
    if (total > gpu_most)
        gpu_most = total;
    if (fragments > gpu_most_frags)
        gpu_most_frags = fragments;
    if (gpu_count >= 120)
        gpu_line();
}

// the raster asks for the resolution each pad read (patch 0006)
uint32_t native_resolution_halves(void)
{
    return (uint32_t)nx_halves;
}

static const char *scale_name(int halves)
{
    return halves == 3 ? "1.5X" : halves == 4 ? "2X" : "1X";
}

enum { ITEM_RESUME, ITEM_SAVE, ITEM_WIDE, ITEM_FAST, ITEM_SIZE, ITEM_TRANSLUCENCY, ITEM_VOICES, ITEM_TEXT, ITEM_CONTROLS, ITEM_VOLUME, ITEM_COUNTER, ITEM_CAPTURE, ITEM_TRIALS, ITEM_QUIT, ITEMS };
// the controls page's items: the controls, then DEFAULTS and BACK
enum { PAGE_MAIN, PAGE_CONTROLS };
// the page's rows: the six Dreamcast buttons, TALK, D-PAD, CAMERA STICK, IDLE LINES, DEFAULTS, BACK
#define CTL_SHOWN CTLS
#define CTL_DPAD CTLS
#define CTL_CAMERA (CTLS + 1)
#define CTL_IDLE (CTLS + 2)
#define CTL_DEFAULTS (CTLS + 3)
#define CTL_BACK (CTLS + 4)
#define CTL_ITEMS (CTLS + 5)
static const char *const camera_names[] = {"OFF", "ON", "INVERTED"};
static const char *const camera_settings[] = {"off", "on", "inverted"};
static int page;
static int ctl_cursor;

static int cursor;
static int quit_armed;

// the settings file, written whole from the values in force
static void save_settings(void)
{
    char disc[256] = DIR "/disc";
    FILE *f = fopen(SETTINGS, "r");
    if (f) {
        char line[300];
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "disc ", 5)) {
                line[strcspn(line, "\r\n")] = 0;
                snprintf(disc, sizeof disc, "%s", line + 5);
            }
        fclose(f);
    }
    f = fopen(SETTINGS, "w");
    if (!f) {
        fprintf(stderr, "menu: can't write %s\n", SETTINGS);
        return;
    }
    fprintf(f,
            "# SonicAdventureRe-nx settings (written by the port menu: Minus and + in the game)\n"
            "disc %s\n"
            "# 4:3 or 16:9\n"
            "aspect %s\n"
            "# 30 (the game's own) or 60\n"
            "fps %s\n"
            "vsync 1\n"
            "sound 1\n"
            "# the decomp's picture size, kept at 1; the port's own resolution is scale below\n"
            "resolution %d\n"
            "# the port's resolution: 1x (640x480), 1.5x (960x720) or 2x (1280x960)\n"
            "scale %s\n"
            "# translucency: accurate (per pixel, as the Dreamcast), effects (accurate, but smoke\n"
            "# and dust per triangle) or fast (per triangle)\n"
            "translucency %s\n"
            "# the port's captures for Claude (stick clicks): 1 on, 0 off; on costs speed\n"
            "capture %d\n"
            "# the port's own: sound volume in percent, the frame counter 1 on 0 off\n"
            "volume %d\n"
            "counter %d\n"
            "# the English layer (english/, made from a US disc): english or japanese, each\n"
            "voices %s\n"
            "text %s\n"
            "# the controls (CONTROLS in the menu): the Switch button for each Dreamcast button,\n"
            "# a b x y l r zl zr l+zl r+zr; talk also minus (a tap alone) or none\n"
            "button_a %s\nbutton_b %s\nbutton_x %s\nbutton_y %s\nbutton_l %s\nbutton_r %s\ntalk %s\n"
            "# the D-pad: look (its own, as on the Dreamcast: standing still it looks round in first\n"
            "# person) or move (it walks, like the stick)\n"
            "dpad %s\n"
            "# the right stick turns the camera (as the Dreamcast's L and R): on, inverted or off\n"
            "camera_stick %s\n"
            "# a character standing still says its idle line as it fidgets: 1 on, 0 off\n"
            "idle_lines %d\n"
            "# trials: open (every Trial stage and mini game, the save left as it is) or saved\n"
            "trials %s\n",
            disc, port_toggles[1] ? "16:9" : "4:3", port_toggles[2] ? "60" : "30", 1,
            nx_halves == 3 ? "1.5x" : nx_halves == 4 ? "2x" : "1x", nx_translucency_fast == 1 ? "fast" : nx_translucency_fast == 2 ? "effects" : "accurate",
            nx_settings_capture, nx_volume, nx_overlay_on,
            nx_voices_english ? "english" : "japanese", nx_text_english ? "english" : "japanese",
            sw_buttons[ctl_map[0]].setting, sw_buttons[ctl_map[1]].setting, sw_buttons[ctl_map[2]].setting,
            sw_buttons[ctl_map[3]].setting, sw_buttons[ctl_map[4]].setting, sw_buttons[ctl_map[5]].setting,
            sw_buttons[ctl_map[6]].setting, nx_dpad_move ? "move" : "look", camera_settings[nx_camera_stick], nx_idle_lines,
            nx_trials_open ? "open" : "saved");
    fclose(f);
    fprintf(stderr, "menu: settings saved: aspect %s, fps %s, resolution %s, capture %d, volume %d, counter %d, "
                    "voices %s, text %s\n",
            port_toggles[1] ? "16:9" : "4:3", port_toggles[2] ? "60" : "30", scale_name(nx_halves), nx_settings_capture,
            nx_volume, nx_overlay_on, nx_voices_english ? "english" : "japanese", nx_text_english ? "english" : "japanese");
}

// the menu's lines as text, for the overlay (nx_overlay.c)
int nx_menu_lines(char lines[][40], int most, int *selected)
{
    int n = 0;
    if (most < ITEMS + 2 || most < CTL_ITEMS + 2)
        return 0;
    if (page == PAGE_CONTROLS) {
        snprintf(lines[n++], 40, "CONTROLS");
        snprintf(lines[n++], 40, " ");
        for (int i = 0; i < CTL_SHOWN; i++)
            snprintf(lines[n++], 40, "%-15s %s", ctl_names[i], sw_buttons[ctl_map[i]].name);
        snprintf(lines[n++], 40, "%-15s %s", "D-PAD", nx_dpad_move ? "MOVE" : "LOOK");
        snprintf(lines[n++], 40, "%-15s %s", "CAMERA STICK", camera_names[nx_camera_stick]);
        snprintf(lines[n++], 40, "%-15s %s", "IDLE LINES", nx_idle_lines ? "ON" : "OFF");
        snprintf(lines[n++], 40, "DEFAULTS");
        snprintf(lines[n++], 40, "BACK");
        *selected = 2 + ctl_cursor;
        return n;
    }
    snprintf(lines[n++], 40, "SONICADVENTURERE-NX");
    snprintf(lines[n++], 40, " ");
    snprintf(lines[n++], 40, "RESUME");
    snprintf(lines[n++], 40, "SAVE NOW");
    snprintf(lines[n++], 40, "WIDESCREEN      %s", port_toggles[1] ? "ON" : "OFF");
    snprintf(lines[n++], 40, "60 FPS          %s", port_toggles[2] ? "ON" : "OFF");
    snprintf(lines[n++], 40, "RESOLUTION      %s", scale_name(nx_halves));
    snprintf(lines[n++], 40, "TRANSLUCENCY    %s",
             nx_translucency_fast == 1 ? "FAST" : nx_translucency_fast == 2 ? "EFFECTS FAST" : "ACCURATE");
    snprintf(lines[n++], 40, "VOICES          %s", nx_voices_english ? "ENGLISH" : "JAPANESE");
    snprintf(lines[n++], 40, "TEXT            %s", nx_text_english ? "ENGLISH" : "JAPANESE");
    snprintf(lines[n++], 40, "CONTROLS");
    snprintf(lines[n++], 40, "VOLUME          %d%%", nx_volume);
    snprintf(lines[n++], 40, "FPS COUNTER     %s", nx_overlay_on ? "ON" : "OFF");
    snprintf(lines[n++], 40, "CAPTURES        %s", nx_settings_capture ? "ON" : "OFF");
    snprintf(lines[n++], 40, "TRIALS          %s", nx_trials_open ? "ALL OPEN" : "AS SAVED");
    snprintf(lines[n++], 40, "%s", quit_armed ? "QUIT: PRESS A AGAIN" : "QUIT GAME");
    *selected = 2 + cursor;
    return n;
}

// a hint under the menu: what the choice under the cursor does
const char *nx_menu_hint(void)
{
    if (page == PAGE_CONTROLS) {
        if (ctl_cursor == CTL_TALK)
            return "THE CHARACTER'S LINE FOR HERE";
        if (ctl_cursor == CTL_DPAD)
            return nx_dpad_move ? "WALKS, LIKE THE STICK" : "STAND STILL: FIRST-PERSON LOOK";
        if (ctl_cursor == CTL_CAMERA)
            return nx_camera_stick ? "RIGHT STICK TURNS THE CAMERA" : "THE TRIGGERS ALONE TURN IT";
        if (ctl_cursor == CTL_IDLE)
            return "A LINE AFTER 30 S STANDING STILL";
        if (ctl_cursor < CTL_SHOWN)
            return "LEFT OR RIGHT CHANGES IT";
        if (ctl_cursor == CTL_DEFAULTS)
            return "A PUTS BACK THE USUAL LAYOUT";
        return "B GOES BACK";
    }
    switch (cursor) {
    case ITEM_SAVE:
        return can_save_now() ? "A SAVES YOUR STORY HERE" : "ADVENTURE AREAS IN A STORY ONLY";
    case ITEM_CONTROLS:
        return "REMAP THE BUTTONS";
    case ITEM_SIZE:
        return "2X MAY NOT HOLD 60 FPS";
    case ITEM_CAPTURE:
        return "NEXT START. ON COSTS SPEED";
    case ITEM_TRIALS:
        return nx_trials_open ? "EVERY STAGE, SAVE UNCHANGED" : "WHAT YOUR SAVE HAS CLEARED";
    case ITEM_TRANSLUCENCY:
        return nx_translucency_fast == 1   ? "PER TRIANGLE: FASTER IN SMOKE"
               : nx_translucency_fast == 2 ? "SMOKE, DUST FAST; REST ACCURATE"
                                           : "PER PIXEL, AS THE DREAMCAST";
    case ITEM_VOICES:
        return nx_english_has_voices() ? "CUTSCENE VOICES" : "NO ENGLISH FILES ON SD";
    case ITEM_TEXT:
        return nx_english_has_text() ? "MENU PICTURES AT NEXT START" : "NO ENGLISH FILES ON SD";
    default:
        return "B OR + RESUMES";
    }
}

static void control_change(int step)
{
    if (ctl_cursor == CTL_DEFAULTS) {
        if (step > 0) {
            for (int i = 0; i < CTLS; i++)
                ctl_map[i] = ctl_defaults[i];
            nx_dpad_move = 0;
            nx_camera_stick = 1;
            nx_idle_lines = 1;
        }
    } else if (ctl_cursor == CTL_DPAD) {
        nx_dpad_move = !nx_dpad_move;
    } else if (ctl_cursor == CTL_CAMERA) {
        nx_camera_stick = (nx_camera_stick + (step < 0 ? 2 : 1)) % 3;
    } else if (ctl_cursor == CTL_IDLE) {
        nx_idle_lines = !nx_idle_lines;
    } else if (ctl_cursor < CTL_SHOWN) {
        int most = ctl_cursor == CTL_TALK ? SW_TALK_CHOICES : SW_DC_CHOICES;
        ctl_map[ctl_cursor] = (ctl_map[ctl_cursor] + (step < 0 ? most - 1 : 1)) % most;
    } else {
        return;
    }
    char line[200];
    nx_controls_describe(line, sizeof line);
    fprintf(stderr, "menu: controls: %s, dpad %s, camera_stick %s, idle_lines %d\n", line,
            nx_dpad_move ? "move" : "look", camera_settings[nx_camera_stick], nx_idle_lines);
    save_settings();
}

static void change(int step)
{
    switch (cursor) {
    case ITEM_WIDE:
        port_toggles[1] = !port_toggles[1];
        port_chosen[1] = 1;
        nx_capture_wide = port_toggles[1];
        break;
    case ITEM_FAST:
        port_toggles[2] = !port_toggles[2];
        port_chosen[2] = 1;
        break;
    case ITEM_SIZE:
        // 1X, 1.5X, 2X, round
        nx_halves = 2 + (nx_halves - 2 + (step < 0 ? 2 : 1)) % 3;
        break;
    case ITEM_TRANSLUCENCY:
        /* ACCURATE, EFFECTS FAST, FAST */
        nx_translucency_fast = nx_translucency_fast == 0 ? 2 : nx_translucency_fast == 2 ? 1 : 0;
        fprintf(stderr, "menu: translucency %s\n",
                nx_translucency_fast == 1 ? "fast" : nx_translucency_fast == 2 ? "effects" : "accurate");
        break;
    case ITEM_VOICES:
        nx_voices_english = !nx_voices_english;
        break;
    case ITEM_TEXT:
        nx_text_english = !nx_text_english;
        break;
    case ITEM_VOLUME:
        nx_volume += step < 0 ? -10 : 10;
        if (nx_volume < 0)
            nx_volume = 0;
        if (nx_volume > 100)
            nx_volume = 100;
        break;
    case ITEM_COUNTER:
        nx_overlay_on = !nx_overlay_on;
        break;
    case ITEM_CAPTURE:
        nx_settings_capture = !nx_settings_capture;
        break;
    case ITEM_TRIALS:
        nx_trials_open = !nx_trials_open;
        fprintf(stderr, "menu: trials %s\n", nx_trials_open ? "all open" : "as saved");
        break;
    default:
        return;
    }
    save_settings();
}

// one step of the menu, from the backend's pad read on the game's thread: 1 while it shows
int native_menu_step(void)
{
    if (!nx_menu_open) {
        if (!nx_menu_request)
            return 0;
        nx_menu_request = 0;
        nx_menu_open = 1;
        cursor = ITEM_RESUME;
        page = PAGE_MAIN;
        quit_armed = 0;
        fprintf(stderr, "menu: open (the game waits)\n");
        return 1;
    }
    u64 down = 0, held = 0;
    if (nx_window_buttons(&down, &held)) {
        // the system asks the program to end: close and let the game end
        nx_menu_open = 0;
        nx_menu_quit = 1;
        fprintf(stderr, "menu: closed, the system ends the program\n");
        return 0;
    }
    if (page == PAGE_CONTROLS) {
        // B or BACK returns to the main page; + resumes the game
        if (down & HidNpadButton_Plus) {
            page = PAGE_MAIN;
            nx_menu_open = 0;
            fprintf(stderr, "menu: closed, the game goes on\n");
            return 0;
        }
        if ((down & HidNpadButton_B) || ((down & HidNpadButton_A) && ctl_cursor == CTL_BACK)) {
            page = PAGE_MAIN;
            return 1;
        }
        if (down & HidNpadButton_AnyUp)
            ctl_cursor = (ctl_cursor + CTL_ITEMS - 1) % CTL_ITEMS;
        if (down & HidNpadButton_AnyDown)
            ctl_cursor = (ctl_cursor + 1) % CTL_ITEMS;
        if (down & HidNpadButton_AnyLeft)
            control_change(-1);
        if (down & (HidNpadButton_AnyRight | HidNpadButton_A))
            control_change(1);
        return 1;
    }
    if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
        nx_menu_open = 0;
        fprintf(stderr, "menu: closed, the game goes on\n");
        return 0;
    }
    if (down & (HidNpadButton_AnyUp)) {
        cursor = (cursor + ITEMS - 1) % ITEMS;
        quit_armed = 0;
    }
    if (down & (HidNpadButton_AnyDown)) {
        cursor = (cursor + 1) % ITEMS;
        quit_armed = 0;
    }
    if (down & HidNpadButton_AnyLeft)
        change(-1);
    if (down & HidNpadButton_AnyRight)
        change(1);
    if (down & HidNpadButton_A) {
        if (cursor == ITEM_RESUME) {
            nx_menu_open = 0;
            fprintf(stderr, "menu: closed, the game goes on\n");
            return 0;
        }
        if (cursor == ITEM_SAVE) {
            if (!can_save_now()) {
                fprintf(stderr, "menu: save now: not here\n");
                return 1;
            }
            save_request_tick = armGetSystemTick();
            save_request = 1;
            nx_menu_open = 0;
            fprintf(stderr, "menu: save now: asked, the game goes on\n");
            return 0;
        }
        if (cursor == ITEM_CONTROLS) {
            page = PAGE_CONTROLS;
            ctl_cursor = 0;
            return 1;
        }
        if (cursor == ITEM_QUIT) {
            if (quit_armed) {
                nx_menu_open = 0;
                nx_menu_quit = 1;
                fprintf(stderr, "menu: quit chosen\n");
                return 0;
            }
            quit_armed = 1;
        } else {
            change(1);
        }
    }
    return 1;
}
