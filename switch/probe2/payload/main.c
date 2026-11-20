// SonicAdventureRe-nx probe 2, the payload: a libnx program linked at a fixed address in a 4 GB
// window, which the loader places in whichever window is free and relocates. If this prints, the
// loader design works: a fixed-address image, moved by whole windows, starts its own libnx.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <switch.h>

// crt0's MOD0 header points at _DYNAMIC; a fixed-address image has no dynamic relocations
__attribute__((used, section(".rodata"))) const unsigned long _DYNAMIC[2] = {0, 0};

// the loader writes here before starting this program (found through the image header)
struct handoff {
    unsigned long magic;
    unsigned long window;
    unsigned long link_window;
    unsigned long relocations;
} probe_handoff = {0x31464f444e4148ULL, 0, 0, 0};

static const char *const where_string = "a pointer in the payload's data";
static const char *const *where = &where_string;

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    FILE *log = fopen("sdmc:/switch/SonicAdventureRe-nx/probe.log", "a");
    char line[512];
    snprintf(line, sizeof line,
             "\n-- payload --\nthe payload is running: main at %p, its data at %p\n"
             "loader said: window %#lx, linked for %#lx, %lu addresses moved\n"
             "a moved pointer reads: \"%s\"\n"
             "heap works: %s\n",
             (void *)&main, (void *)&probe_handoff, probe_handoff.window, probe_handoff.link_window,
             probe_handoff.relocations, *where, malloc(1 << 20) ? "yes (1 MB)" : "NO");
    printf("%s", line);
    if (log) {
        fputs(line, log);
        fputs("payload done\n\n", log);
        fclose(log);
    }
    printf("\nDone. Press + to exit.\n");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
