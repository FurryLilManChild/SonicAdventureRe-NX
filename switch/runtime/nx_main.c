// SonicAdventureRe-nx: the Switch program's main, around the decomp's (runner.c's main, renamed
// native_main at the link). It runs the game's front door in a window: the menus on, as the
// player plays, on the disc in DIR/disc, with DIR/settings.txt (written with defaults where none
// lies) and the memory card DIR/card.bin.
//
// Logs (sdmc:/switch/SonicAdventureRe-nx/, appended): SonicAdventureRe-nx.log (stderr: the game's
// reports and this file's lines) and SonicAdventureRe-nx.out.log (stdout). Each picture's timing
// goes to SonicAdventureRe-nx.times.txt. The screen is the game's: no console.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>

#define DIR "sdmc:/switch/SonicAdventureRe-nx"

int native_main(int argc, char **argv);
int nx_domain_unmap_all(void);
int nx_domain_maps(void);
extern unsigned long native_vsyncs;
extern char native_domain_base[];

const char *nx_program_path = DIR "/SonicAdventureRe-nx.elf";

// the build's commit and the link window, filled by the build (switch/build.sh)
#ifndef NX_BUILD
#define NX_BUILD "unknown"
#endif
extern char __start__[];
// the window the image links for (nx_base.S), a number the loader leaves as it is
extern const u64 nx_link_window_number;

static u64 started;
static volatile int game_done;
static Thread beat;
static int beat_running;

void native_report_state(FILE *stream);
extern volatile unsigned long nx_sound_loops, nx_sound_pieces, nx_sound_played, nx_sound_started;
extern volatile unsigned long nx_sound_returned, nx_sound_waits_empty;
double nx_capture_copy_ms(void);
extern int nx_capture_wide;
extern int nx_capture_enabled;
extern int nx_volume, nx_resolution, nx_settings_capture, nx_overlay_on, nx_halves;
extern int nx_voices_english, nx_text_english;
int nx_english_has_voices(void);
int nx_english_has_text(void);
extern volatile int nx_overlay_fps;

// the clocks now, in MHz: CPU, GPU, memory (sys-clk can change them during a run)
static void clocks(u32 mhz[3])
{
    mhz[0] = mhz[1] = mhz[2] = 0;
    if (R_FAILED(clkrstInitialize()))
        return;
    const PcvModuleId mods[3] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};
    for (int i = 0; i < 3; i++) {
        ClkrstSession s;
        if (R_SUCCEEDED(clkrstOpenSession(&s, mods[i], 3))) {
            u32 hz = 0;
            clkrstGetClockRate(&s, &hz);
            mhz[i] = hz / 1000000;
            clkrstCloseSession(&s);
        }
    }
    clkrstExit();
}

// a line to the log (stderr is the SD card's file)
static void line(const char *fmt, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    fputs(text, stderr);
}

static double seconds(void) { return (double)(armGetSystemTick() - started) / armGetSystemTickFreq(); }

// every 5 s while the game runs: the vsyncs so far and their rate, to the log, so a hang shows
// where it stopped. Its priority is one
// above the main thread's: Horizon never shares a core between threads of one priority until one
// waits, and the game's thread doesn't
static void heartbeat(void *arg)
{
    (void)arg;
    unsigned long last = ~0UL;
    while (!game_done) {
        svcSleepThread(5000000000LL);
        if (game_done)
            break;
        unsigned long v = native_vsyncs;
        u32 mhz[3];
        clocks(mhz);
        fprintf(stderr,
                "[heartbeat] %.1f s: %d frames a second on screen; %lu vsyncs, %.1f a second%s; clocks %u/%u/%u MHz; sound: thread %s, %lu loops, "
                "%lu buffers queued, %lu played and returned (%lu waits with none); replay copy %.2f ms a picture\n",
                seconds(), nx_overlay_fps, v, last == ~0UL ? 0.0 : (v - last) / 5.0,
                v == last ? " (no progress since the last beat)" : "", mhz[0], mhz[1], mhz[2],
                nx_sound_started ? "running" : "NOT STARTED", nx_sound_loops, nx_sound_pieces, nx_sound_returned,
                nx_sound_waits_empty,
                nx_capture_copy_ms());
        last = v;
    }
}

// a fault anywhere: what and where; the build's ELF resolves an address with addr2line once the
// window's distance from the link window is added
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof __nx_exception_stack;

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    u64 window = (u64)native_domain_base, link = nx_link_window_number;
    FILE *f = fopen(DIR "/SonicAdventureRe-nx.crash.log", "a");
    FILE *outs[2] = {f, stderr};
    for (int k = 0; k < 2; k++) {
        FILE *o = outs[k];
        if (!o)
            continue;
        fprintf(o, "\n== crash, build %s, %.1f s in, %lu vsyncs ==\n", NX_BUILD, seconds(), native_vsyncs);
        fprintf(o, "error %u, window %#lx (linked for %#lx): add %#lx to an address for addr2line\n",
                ctx->error_desc, window, link, link - window);
        fprintf(o, "pc %#lx lr %#lx sp %#lx far %#lx\n", ctx->pc.x, ctx->lr.x, ctx->sp.x, ctx->far.x);
        for (int i = 0; i < 29; i++)
            fprintf(o, "x%-2d %016lx%s", i, ctx->cpu_gprs[i].x, i % 4 == 3 ? "\n" : "  ");
        fprintf(o, "\nfp %016lx\n", ctx->fp.x);
        fflush(o);
    }
    if (f)
        fclose(f);
    svcExitProcess();
}

// the run's end. runner.c ends with _exit on the stack of one of the game's threads, which lies in
// the domain, so the end moves to a stack of its own first (nx_end), then logs the result,
// undoes the domain's maps and leaves through the loader, which unmaps the image
void __real__exit(int status) __attribute__((noreturn));
alignas(16) __attribute__((used)) u8 end_stack[0x10000]; // used by __wrap__exit, below

// each step of the end, flushed, so a hang shows where it stopped
static void step(const char *what)
{
    fprintf(stderr, "nx: end: %s (%.1f s)\n", what, seconds());
    fflush(stderr);
}

static void __attribute__((noreturn, used)) nx_finish(int status)
{
    step("_exit reached, on the end's own stack");
    game_done = 1;
    if (beat_running) {
        threadWaitForExit(&beat);
        threadClose(&beat);
        beat_running = 0;
    }
    step("heartbeat stopped");
    double t = seconds();
    step("the game's report");
    native_report_state(stderr);
    fflush(stdout);
    step("undoing the domain's maps");
    int maps = nx_domain_maps();
    int undone = nx_domain_unmap_all();
    fprintf(stderr, "nx: run over after %.1f s, status %d; %d domain maps undone: %s\n", t, status, maps,
            undone ? "SOME FAILED" : "ok");
    fprintf(stderr, "nx: returning to the menu through the loader\n\n");
    fflush(stderr);
    __real__exit(status);
}

// exit would run the C library's handlers (C++ destructors among them) before _exit. They tore
// down the renderer and the sound under threads still running, and the console crashed after
// "exit(0) called" on every quit through exit. The game registers no handler of its own (no
// atexit in the decomp), so exit now stops the sound, flushes the files, and takes the same end
// as _exit, which leaves cleanly
void __wrap__exit(int status) __attribute__((noreturn));
void nx_sound_stop(void);
void __wrap_exit(int status) __attribute__((noreturn));
void __wrap_exit(int status)
{
    fprintf(stderr, "nx: end: exit(%d) called; the sound stops and the end goes on as _exit's (%.1f s)\n", status,
            seconds());
    fflush(stderr);
    nx_sound_stop();
    step("sound stopped");
    fflush(NULL);
    __wrap__exit(status);
}

__asm__(".text\n"
        ".globl __wrap__exit\n"
        ".type __wrap__exit, %function\n"
        "__wrap__exit:\n"
        "    adrp x1, end_stack + 0x10000\n"
        "    add x1, x1, :lo12:end_stack + 0x10000\n"
        "    mov sp, x1\n"
        "    b nx_finish\n");


// the settings the first run writes: the game's own defaults, said out loud, so they can be changed
static const char default_settings[] =
    "# SonicAdventureRe-nx settings (the decomp's own: name value, # starts a note)\n"
    "# the port's own defaults: 16:9 at 60 fps\n"
    "disc " DIR "/disc\n"
    "# 4:3 or 16:9\n"
    "aspect 16:9\n"
    "# 30 (the game's own) or 60\n"
    "fps 60\n"
    "vsync 1\n"
    "sound 1\n"
    "# the decomp's picture size, kept at 1; the port's own resolution is scale below\n"
    "resolution 1\n"
    "# the port's resolution: 1x (640x480), 1.5x (960x720) or 2x (1280x960)\n"
    "scale 1x\n"
    "# the port's captures for Claude (stick clicks): 1 on, 0 off; on costs speed\n"
    "capture 0\n"
    "# the port's own: sound volume in percent, the frame counter 1 on 0 off\n"
    "volume 100\n"
    "counter 1\n"
    "# the English layer (english/, made from a US disc): english or japanese, each\n"
    "voices english\n"
    "text english\n";

// the settings in force, logged whole (so a change is seen applied), and whether they ask for
// 16:9 (the captures stretch to it)
static void settings_log(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char text[256];
    int scale_seen = 0;
    line("settings (%s):\n", path);
    while (fgets(text, sizeof text, f)) {
        char *note = strchr(text, '#');
        if (note)
            *note = 0;
        // no sscanf: the game's C brings its own (events/shared/sdk), which the link may pick
        char name[32] = "", value[200] = "";
        char *p = text + strspn(text, " \t\r\n");
        size_t n = strcspn(p, " \t\r\n");
        if (n && n < sizeof name) {
            memcpy(name, p, n);
            name[n] = 0;
            p += n;
            p += strspn(p, " \t");
            size_t m = strcspn(p, " \t\r\n");
            if (m < sizeof value) {
                memcpy(value, p, m);
                value[m] = 0;
            }
        }
        if (name[0] && value[0]) {
            line("  %s %s\n", name, value);
            if (!strcmp(name, "aspect"))
                nx_capture_wide = !strcmp(value, "16:9");
            if (!strcmp(name, "capture"))
                nx_settings_capture = nx_capture_enabled = !strcmp(value, "1");
            if (!strcmp(name, "resolution"))
                nx_resolution = atoi(value);
            if (!strcmp(name, "scale")) {
                nx_halves = !strcmp(value, "1.5x") ? 3 : !strcmp(value, "2x") ? 4 : 2;
                scale_seen = 1;
            }
            if (!strcmp(name, "volume"))
                nx_volume = atoi(value);
            if (!strcmp(name, "voices"))
                nx_voices_english = strcmp(value, "japanese") != 0;
            if (!strcmp(name, "text"))
                nx_text_english = strcmp(value, "japanese") != 0;
            if (!strcmp(name, "counter"))
                nx_overlay_on = atoi(value) != 0;
        }
    }
    fclose(f);
    // an older settings file sized the picture with the decomp's resolution alone
    if (!scale_seen)
        nx_halves = nx_resolution == 2 ? 4 : 2;
    line("  (the port's resolution: %s)\n", nx_halves == 3 ? "1.5x" : nx_halves == 4 ? "2x" : "1x");
    line("  (English: voices %s, text %s; the english folder holds voices: %s, subtitles: %s)\n",
         nx_voices_english ? "english" : "japanese", nx_text_english ? "english" : "japanese",
         nx_english_has_voices() ? "yes" : "no", nx_english_has_text() ? "yes" : "no");
}

static void settings_exist(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return;
    }
    if ((f = fopen(path, "w"))) {
        fputs(default_settings, f);
        fclose(f);
        line("nx: wrote the default settings to %s\n", path);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    started = armGetSystemTick();
    mkdir(DIR, 0777);
    freopen(DIR "/SonicAdventureRe-nx.log", "a", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);
    freopen(DIR "/SonicAdventureRe-nx.out.log", "a", stdout);
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    line("\n== SonicAdventureRe-nx (build %s): the game, in a window (Vulkan, NVK)\n", NX_BUILD);
    line("image at %p in window %p (linked for %p)\n", (void *)__start__, (void *)native_domain_base,
         (void *)nx_link_window_number);
    u32 clk[3];
    clocks(clk);
    line("clocks: CPU %u MHz, GPU %u MHz, memory %u MHz; %s\n", clk[0], clk[1], clk[2],
         appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld");
    settings_exist(DIR "/settings.txt");
    settings_log(DIR "/settings.txt");
    if (R_SUCCEEDED(threadCreate(&beat, heartbeat, NULL, NULL, 0x4000, 0x2b, -2))) {
        threadStart(&beat);
        beat_running = 1;
    }
    static char *args[] = {"play",
                           "--settings", DIR "/settings.txt",
                           "--card", DIR "/card.bin",
                           "--window", DIR "/disc",
                           "--times", DIR "/SonicAdventureRe-nx.times.txt",
                           NULL};
    line("starting the game: play --settings %s --card %s --window %s --times %s\n\n", args[2], args[4], args[6],
         args[8]);
    int rc = native_main(9, args);
    fprintf(stderr, "nx: the game returned %d\n", rc);
    _exit(rc);
}
