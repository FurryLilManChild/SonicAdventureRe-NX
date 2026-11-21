// SonicAdventureRe-nx: the port menu. Minus and + together open it over the game, which waits
// (the backend holds it in its pad read, patch 0006, and shows the last picture again each vsync
// with this menu over it). Sound goes quiet while it shows.
//
//   up and down choose, left and right (or A) change, B or + resumes.
//
// Widescreen and 60 fps change at once (the decomp's own port toggles); picture size and
// captures take effect at the next start. Every change is written to settings.txt.
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

volatile int nx_menu_open;     // read by the sound thread: quiet while 1
volatile int nx_menu_request;  // set by the window on Minus and +
volatile int nx_menu_quit;     // set by Quit: the window then ends the game
int nx_volume = 100;           // percent, read by the sound thread
int nx_resolution = 1;         // settings.txt's resolution (0 fits the screen)
int nx_settings_capture;       // the captures setting for the next start

enum { ITEM_RESUME, ITEM_WIDE, ITEM_FAST, ITEM_SIZE, ITEM_VOLUME, ITEM_COUNTER, ITEM_CAPTURE, ITEM_QUIT, ITEMS };

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
            "# the picture's size: 1 is the Dreamcast's 640x480, 2 is twice that, 0 fits the screen\n"
            "resolution %d\n"
            "# the port's captures for Claude (stick clicks): 1 on, 0 off; on costs speed\n"
            "capture %d\n"
            "# the port's own: sound volume in percent, the frame counter 1 on 0 off\n"
            "volume %d\n"
            "counter %d\n",
            disc, port_toggles[1] ? "16:9" : "4:3", port_toggles[2] ? "60" : "30", nx_resolution,
            nx_settings_capture, nx_volume, nx_overlay_on);
    fclose(f);
    fprintf(stderr, "menu: settings saved: aspect %s, fps %s, resolution %d, capture %d, volume %d, counter %d\n",
            port_toggles[1] ? "16:9" : "4:3", port_toggles[2] ? "60" : "30", nx_resolution, nx_settings_capture,
            nx_volume, nx_overlay_on);
}

// the menu's lines as text, for the overlay (nx_overlay.c)
int nx_menu_lines(char lines[][40], int most, int *selected)
{
    static const char *sizes[3] = {"FIT SCREEN", "640X480", "1280X960"};
    int n = 0;
    if (most < ITEMS + 2)
        return 0;
    snprintf(lines[n++], 40, "SONICADVENTURERE-NX");
    snprintf(lines[n++], 40, " ");
    snprintf(lines[n++], 40, "RESUME");
    snprintf(lines[n++], 40, "WIDESCREEN      %s", port_toggles[1] ? "ON" : "OFF");
    snprintf(lines[n++], 40, "60 FPS          %s", port_toggles[2] ? "ON" : "OFF");
    snprintf(lines[n++], 40, "PICTURE SIZE    %s", sizes[nx_resolution < 0 || nx_resolution > 2 ? 1 : nx_resolution]);
    snprintf(lines[n++], 40, "VOLUME          %d%%", nx_volume);
    snprintf(lines[n++], 40, "FPS COUNTER     %s", nx_overlay_on ? "ON" : "OFF");
    snprintf(lines[n++], 40, "CAPTURES        %s", nx_settings_capture ? "ON" : "OFF");
    snprintf(lines[n++], 40, "%s", quit_armed ? "QUIT: PRESS A AGAIN" : "QUIT GAME");
    *selected = 2 + cursor;
    return n;
}

// a hint under the menu: what the choice under the cursor does
const char *nx_menu_hint(void)
{
    switch (cursor) {
    case ITEM_SIZE:
        return "TAKES EFFECT AT THE NEXT START";
    case ITEM_CAPTURE:
        return "NEXT START. ON COSTS SPEED";
    default:
        return "B OR + RESUMES";
    }
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
    case ITEM_SIZE: {
        // in the order shown: 640x480 (1), 1280x960 (2), fit (0)
        static const int order[3] = {1, 2, 0};
        int at = 0;
        for (int i = 0; i < 3; i++)
            if (order[i] == nx_resolution)
                at = i;
        nx_resolution = order[(at + (step < 0 ? 2 : 1)) % 3];
        break;
    }
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
