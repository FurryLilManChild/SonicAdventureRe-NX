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
        fprintf(stderr, "[heartbeat] %.1f s: %lu vsyncs, %.1f a second%s\n", seconds(), v,
                last == ~0UL ? 0.0 : (v - last) / 5.0, v == last ? " (no progress since the last beat)" : "");
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

// exit runs the C library's handlers (atexit, C++ destructors) before _exit: logged, so a hang
// in them shows
void __real_exit(int status) __attribute__((noreturn));
void __wrap_exit(int status) __attribute__((noreturn));
void __wrap_exit(int status)
{
    fprintf(stderr, "nx: end: exit(%d) called; the C library's handlers run next (%.1f s)\n", status, seconds());
    fflush(stderr);
    __real_exit(status);
}

void __wrap__exit(int status) __attribute__((noreturn));
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
    "disc " DIR "/disc\n"
    "# 4:3 or 16:9\n"
    "aspect 4:3\n"
    "# 30 (the game's own) or 60\n"
    "fps 30\n"
    "vsync 1\n"
    "sound 1\n"
    "# the picture's size: 1 is the Dreamcast's 640x480, 2 is twice that, 0 fits the screen\n"
    "resolution 1\n";

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
    u64 clk[3] = {0};
    if (R_SUCCEEDED(clkrstInitialize())) {
        const PcvModuleId mods[3] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};
        for (int i = 0; i < 3; i++) {
            ClkrstSession s;
            if (R_SUCCEEDED(clkrstOpenSession(&s, mods[i], 3))) {
                u32 hz = 0;
                clkrstGetClockRate(&s, &hz);
                clk[i] = hz;
                clkrstCloseSession(&s);
            }
        }
        clkrstExit();
    }
    line("clocks: CPU %lu MHz, GPU %lu MHz, memory %lu MHz; %s\n", clk[0] / 1000000, clk[1] / 1000000,
         clk[2] / 1000000, appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld");
    settings_exist(DIR "/settings.txt");
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
