// SonicAdventureRe-nx: the Switch program's main, around the decomp's (runner.c's main, renamed
// native_main at the link). Switch Test 1 runs the game headless: Emerald Coast for 600 vsyncs,
// as the decomp's `make native` does, and reports the frames it drew.
//
// Logs (sdmc:/switch/SonicAdventureRe-nx/, appended): SonicAdventureRe-nx.log (stdout and this
// file's lines) and SonicAdventureRe-nx.err.log (stderr). The screen shows the progress.
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

// a line to the screen (stdout is the console) and the log (stderr is the SD card's file)
static void line(const char *fmt, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    fputs(text, stdout);
    consoleUpdate(NULL);
    fputs(text, stderr);
}

static double seconds(void) { return (double)(armGetSystemTick() - started) / armGetSystemTickFreq(); }

// every 2 s while the game runs: the vsyncs so far, to the log and the screen (the main thread is
// in the game, so this thread alone draws), so a hang shows where it stopped. Its priority is one
// above the main thread's: Horizon never shares a core between threads of one priority until one
// waits, and the game's thread doesn't
static void heartbeat(void *arg)
{
    (void)arg;
    unsigned long last = ~0UL;
    while (!game_done) {
        svcSleepThread(2000000000LL);
        if (game_done)
            break;
        unsigned long v = native_vsyncs;
        fprintf(stderr, "[heartbeat] %.1f s: %lu vsyncs%s\n", seconds(), v,
                v == last ? " (no progress since the last beat)" : "");
        printf("\r%.0f s: %lu of 600 vsyncs   ", seconds(), v);
        consoleUpdate(NULL);
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
        svcSleepThread(16000000);
    }
}

// the run's end. runner.c ends with _exit on the stack of one of the game's threads, which lies in
// the domain, so the end moves to a stack of its own first (nx_end), then shows the result,
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
    fflush(stderr);
    consoleClear();
    printf("SonicAdventureRe-nx Switch Test 1 (build %s)\n\n", NX_BUILD);
    printf("Run over after %.1f s, status %d.\n", t, status);
    printf("Where the game got to:\n  %lu vsyncs, ", native_vsyncs);
    step("the game's report");
    native_report_state(stderr);
    native_report_state(stdout);
    printf("\n(the PC build reaches 396 frames in 600 vsyncs)\n\n");
    consoleUpdate(NULL);
    step("undoing the domain's maps");
    int maps = nx_domain_maps();
    int undone = nx_domain_unmap_all();
    fprintf(stderr, "nx: run over after %.1f s, status %d; %d domain maps undone: %s\n", t, status, maps,
            undone ? "SOME FAILED" : "ok");
    fprintf(stderr, "nx: returning to the menu through the loader\n\n");
    fflush(stderr);
    printf("Domain maps undone: %s\nPress + to exit.\n", undone ? "SOME FAILED" : "ok");
    consoleUpdate(NULL);
    step("waiting for +");
    wait_for_plus();
    step("+ pressed, leaving");
    consoleExit(NULL);
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


int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    started = armGetSystemTick();
    consoleInit(NULL);
    mkdir(DIR, 0777);
    // stderr, where the game reports, goes to the SD card; stdout stays the screen
    freopen(DIR "/SonicAdventureRe-nx.log", "a", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);
    line("SonicAdventureRe-nx Switch Test 1 (build %s)\n", NX_BUILD);
    line("Emerald Coast, headless, 600 vsyncs. No picture: this checks the game runs.\n\n");
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
    line("clocks: CPU %lu MHz, GPU %lu MHz, memory %lu MHz\n", clk[0] / 1000000, clk[1] / 1000000,
         clk[2] / 1000000);
    if (R_SUCCEEDED(threadCreate(&beat, heartbeat, NULL, NULL, 0x4000, 0x2b, -2))) {
        threadStart(&beat);
        beat_running = 1;
    }
    static char *args[] = {"play", DIR "/disc", "600", "1", "0", NULL};
    line("starting the game: play %s 600 1 0\n\n", args[1]);
    int rc = native_main(5, args);
    fprintf(stderr, "nx: the game returned %d\n", rc);
    _exit(rc);
}
