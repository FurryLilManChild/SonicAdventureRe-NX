// SonicAdventureRe-nx: the decomp's window (engine/pc/window.h) on Horizon, in place of its SDL3
// window.cpp. It gives the raster a VI surface on the default NWindow (NVK's VK_NN_vi_surface),
// the pad from libnx's HID, and sound through audout (resampled from SA_PC_SOUND_RATE).
//
// The pad is positional, as the Dreamcast's face buttons lie: the Dreamcast's A (bottom) is the
// Switch's B, its B (right) is A, its X (left) is Y and its Y (top) is X. Start is +. ZL and ZR
// (and L and R) are the analog triggers, full or nothing. The left stick is the Dreamcast's stick,
// squared (a stick's round reach stretched to the square a Dreamcast pad reaches), and the d-pad
// moves it too, as the PC build does. Minus and + together end the game. The stick clicks are
// the port's captures (nx_capture.c): right, a picture; left, the last 2 seconds.
#include "window.h"

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <new>
#include <string>

#include <switch.h>

#define VK_USE_PLATFORM_VI_NN
#include <vulkan/vulkan.h>

#include "sa_pc.h"

extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *name);
// nx_capture.c: a stick click asks for a capture (1 a picture, 2 a replay)
extern "C" volatile int nx_capture_request;

// the sound thread's progress, for the heartbeat (nx_main.c): loops run, pieces rendered and
// queued, and the voice's played samples
extern "C" {
volatile unsigned long nx_sound_loops, nx_sound_pieces, nx_sound_played, nx_sound_started;
volatile unsigned long nx_sound_returned, nx_sound_waits_empty;
}

namespace pc {

namespace {

enum : uint32_t {
    PAD_C = 1 << 0,
    PAD_B = 1 << 1,
    PAD_A = 1 << 2,
    PAD_START = 1 << 3,
    PAD_UP = 1 << 4,
    PAD_DOWN = 1 << 5,
    PAD_LEFT = 1 << 6,
    PAD_RIGHT = 1 << 7,
    PAD_Z = 1 << 8,
    PAD_Y = 1 << 9,
    PAD_X = 1 << 10,
    PAD_TR = 1 << 16,
    PAD_TL = 1 << 17,
};

// the sound: audout, the Switch's plain audio output (48 kHz stereo), fed PIECES buffers of PIECE
// frames (about 21 ms each). The game renders at SA_PC_SOUND_RATE (44.1 kHz), which feed()
// resamples, linearly, to the output's rate
constexpr int PIECE = 1024;
constexpr int PIECES = 4;
constexpr size_t PIECE_BYTES = (PIECE * 2 * sizeof(int16_t) + 0xfff) & ~size_t(0xfff);
// the game's frames rendered at a time, ahead of the resampler
constexpr int SOURCE = 512;

// a stick's axis from -1 to 1, with its partner's for the reach: past dead, out to 1, and where
// square, stretched so a push to a corner reaches the corner
float stick_axis(float raw, float partner, float dead, bool square)
{
    const float mag = sqrtf(raw * raw + partner * partner);
    if (mag <= dead || mag <= 0.0f)
        return 0.0f;
    const float strength = std::min(1.0f, (std::min(mag, 1.0f) - dead) / (1.0f - dead));
    float out = raw / mag * strength;
    if (square) {
        const float other = partner / mag * strength;
        const float edge = std::max(fabsf(out), fabsf(other));
        if (edge > 0.0f)
            out *= strength / edge;
    }
    return std::clamp(out, -1.0f, 1.0f);
}

// -1 to 1 as the Dreamcast's -128 to 127
int16_t dc_axis(float v)
{
    const float scaled = v < 0 ? v * 128.0f : v * 127.0f;
    return static_cast<int16_t>(std::clamp(lrintf(scaled), -128L, 127L));
}

} // namespace

struct Window {
    PadState pad;
    uint32_t held; // the last sample's, which press and release differ from
    uint32_t width, height;

    // sound
    SoundRender render;
    void *user;
    bool sound_open;
    volatile bool sound_stop;
    Thread sound_thread;
    u32 out_rate;
    void *pool; // the pieces' samples, 0x1000-aligned
    AudioOutBuffer pieces[PIECES];
    // the resampler: the game's frames rendered and not yet used, and the place between them
    int16_t source[2 * (SOURCE + 1)];
    int source_count, source_at;
    double phase;
};

Window *window_open(const char *title, uint32_t width, uint32_t height, uint32_t flags,
                    const char *settings, const Image *icon, std::string &why)
{
    (void)title;
    (void)width;
    (void)height;
    (void)flags;
    (void)settings;
    (void)icon;
    Window *w = new (std::nothrow) Window{};
    if (w == nullptr) {
        why = "no memory for the window";
        return nullptr;
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&w->pad);
    padUpdate(&w->pad);
    w->width = 1280;
    w->height = 720;
    NWindow *win = nwindowGetDefault();
    nwindowSetDimensions(win, w->width, w->height);
    fprintf(stderr, "nx: window %ux%u (handheld size; docked output is scaled by the system)\n", w->width,
            w->height);
    return w;
}

void window_close(Window *window)
{
    if (window == nullptr)
        return;
    window_sound_close(window);
    delete window;
}

void window_alert(const char *title, const char *words)
{
    fprintf(stderr, "nx: alert: %s: %s\n", title ? title : "", words ? words : "");
    fflush(stderr);
}

const char *const *window_extensions(uint32_t &count)
{
    static const char *const names[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME};
    count = 2;
    return names;
}

bool window_surface(Window *window, void *instance, uint64_t &surface)
{
    if (window == nullptr)
        return false;
    VkInstance vk = static_cast<VkInstance>(instance);
    auto create = reinterpret_cast<PFN_vkCreateViSurfaceNN>(vkGetInstanceProcAddr(vk, "vkCreateViSurfaceNN"));
    if (create == nullptr) {
        fprintf(stderr, "nx: the Vulkan instance has no vkCreateViSurfaceNN\n");
        return false;
    }
    VkViSurfaceCreateInfoNN info{};
    info.sType = VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN;
    info.window = nwindowGetDefault();
    VkSurfaceKHR made = VK_NULL_HANDLE;
    VkResult r = create(vk, &info, nullptr, &made);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "nx: vkCreateViSurfaceNN failed: %d\n", static_cast<int>(r));
        return false;
    }
    surface = reinterpret_cast<uint64_t>(made);
    return true;
}

void window_size(Window *window, uint32_t &width, uint32_t &height)
{
    width = window ? window->width : 0;
    height = window ? window->height : 0;
}

bool window_pump(Window *window)
{
    if (!appletMainLoop())
        return true;
    if (window == nullptr)
        return false;
    padUpdate(&window->pad);
    const u64 held = padGetButtons(&window->pad);
    const u64 down = padGetButtonsDown(&window->pad);
    if (down & HidNpadButton_StickR)
        nx_capture_request |= 1;
    if (down & HidNpadButton_StickL)
        nx_capture_request |= 2;
    if ((held & HidNpadButton_Minus) && (held & HidNpadButton_Plus)) {
        fprintf(stderr, "nx: Minus and + held: ending the game\n");
        return true;
    }
    return false;
}

sa_result window_pad(Window *window, uint32_t port, sa_pad &pad)
{
    if (window == nullptr || port != 0)
        return SA_EMPTY;
    if (!padIsConnected(&window->pad))
        return SA_EMPTY;
    const u64 b = padGetButtons(&window->pad);
    uint32_t held = 0;
    held |= (b & HidNpadButton_B) ? PAD_A : 0;
    held |= (b & HidNpadButton_A) ? PAD_B : 0;
    held |= (b & HidNpadButton_Y) ? PAD_X : 0;
    held |= (b & HidNpadButton_X) ? PAD_Y : 0;
    held |= (b & HidNpadButton_Plus) ? PAD_START : 0;
    held |= (b & HidNpadButton_Up) ? PAD_UP : 0;
    held |= (b & HidNpadButton_Down) ? PAD_DOWN : 0;
    held |= (b & HidNpadButton_Left) ? PAD_LEFT : 0;
    held |= (b & HidNpadButton_Right) ? PAD_RIGHT : 0;

    const HidAnalogStickState l = padGetStickPos(&window->pad, 0);
    const HidAnalogStickState r = padGetStickPos(&window->pad, 1);
    // HID's y is up; the Dreamcast's is down
    const float lx = l.x / 32767.0f, ly = -l.y / 32767.0f;
    const float rx = r.x / 32767.0f, ry = -r.y / 32767.0f;
    pad.x1 = dc_axis(stick_axis(lx, ly, 0.0f, true));
    pad.y1 = dc_axis(stick_axis(ly, lx, 0.0f, true));
    pad.x2 = dc_axis(stick_axis(rx, ry, 0.0f, true));
    pad.y2 = dc_axis(stick_axis(ry, rx, 0.0f, true));
    if (held & (PAD_LEFT | PAD_RIGHT))
        pad.x1 = held & PAD_LEFT ? -128 : 127;
    if (held & (PAD_UP | PAD_DOWN))
        pad.y1 = held & PAD_UP ? -128 : 127;

    const bool left = b & (HidNpadButton_ZL | HidNpadButton_L);
    const bool right = b & (HidNpadButton_ZR | HidNpadButton_R);
    pad.l = left ? 255 : 0;
    pad.r = right ? 255 : 0;
    held |= left ? PAD_TL : 0;
    held |= right ? PAD_TR : 0;

    pad.on = held;
    pad.press = held & ~window->held;
    pad.release = window->held & ~held;
    window->held = held;
    return SA_OK;
}

sa_result window_rumble(Window *, uint32_t, uint16_t, uint16_t, uint32_t, uint32_t)
{
    return SA_EMPTY;
}

namespace {

// the next output frame: the game's frames at the place between source[at] and source[at + 1]
void resampled(Window *w, int16_t *out, int frames)
{
    const double step = (double)SA_PC_SOUND_RATE / w->out_rate;
    for (int f = 0; f < frames; f++) {
        // keep two of the game's frames ahead: render more where fewer are left
        while (w->source_at + 1 >= w->source_count) {
            // the last frame stays, as the first of the next run
            int keep = w->source_count > 0 ? 1 : 0;
            if (keep) {
                w->source[0] = w->source[2 * (w->source_count - 1)];
                w->source[1] = w->source[2 * (w->source_count - 1) + 1];
            }
            w->render(w->user, w->source + 2 * keep, SOURCE);
            w->source_at -= w->source_count - keep;
            if (w->source_at < 0)
                w->source_at = 0;
            w->source_count = keep + SOURCE;
        }
        const int16_t *a = w->source + 2 * w->source_at, *b = a + 2;
        const float t = (float)w->phase;
        out[2 * f] = (int16_t)lrintf(a[0] + (b[0] - a[0]) * t);
        out[2 * f + 1] = (int16_t)lrintf(a[1] + (b[1] - a[1]) * t);
        w->phase += step;
        while (w->phase >= 1.0) {
            w->phase -= 1.0;
            w->source_at++;
        }
    }
}

void fill_and_queue(Window *w, AudioOutBuffer *p)
{
    resampled(w, static_cast<int16_t *>(p->buffer), PIECE);
    p->data_size = PIECE * 2 * sizeof(int16_t);
    p->data_offset = 0;
    armDCacheFlush(p->buffer, p->buffer_size);
    Result rc = audoutAppendAudioOutBuffer(p);
    if (R_FAILED(rc))
        fprintf(stderr, "nx: sound: audoutAppendAudioOutBuffer failed: %#x\n", rc);
    nx_sound_pieces++;
}

// the sound's thread: every buffer audout gives back is filled again and queued. It waits on
// audout (at most 100 ms a wait, so a stop is seen)
void feed(void *arg)
{
    Window *w = static_cast<Window *>(arg);
    nx_sound_started = 1;
    fprintf(stderr, "nx: sound thread running\n");
    for (int i = 0; i < PIECES; i++)
        fill_and_queue(w, &w->pieces[i]);
    while (!w->sound_stop) {
        AudioOutBuffer *released = nullptr;
        u32 count = 0;
        Result rc = audoutWaitPlayFinish(&released, &count, 100000000ULL);
        nx_sound_loops++;
        if (R_FAILED(rc) || released == nullptr || count == 0) {
            nx_sound_waits_empty++;
            continue;
        }
        nx_sound_returned += count;
        nx_sound_played = nx_sound_returned * PIECE;
        fill_and_queue(w, released);
    }
}

} // namespace

bool window_sound_open(Window *window, SoundRender render, void *user, std::string &why)
{
    if (window == nullptr) {
        why = "no window";
        return false;
    }
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
        why = "audoutInitialize failed: " + std::to_string(rc);
        return false;
    }
    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        audoutExit();
        why = "audoutStartAudioOut failed: " + std::to_string(rc);
        return false;
    }
    window->out_rate = audoutGetSampleRate();
    if (window->out_rate == 0)
        window->out_rate = 48000;
    window->pool = memalign(0x1000, PIECES * PIECE_BYTES);
    if (window->pool == nullptr) {
        audoutStopAudioOut();
        audoutExit();
        why = "no memory for the sound's buffers";
        return false;
    }
    memset(window->pool, 0, PIECES * PIECE_BYTES);
    for (int i = 0; i < PIECES; i++) {
        AudioOutBuffer &p = window->pieces[i];
        p = AudioOutBuffer{};
        p.buffer = static_cast<u8 *>(window->pool) + i * PIECE_BYTES;
        p.buffer_size = PIECE_BYTES;
    }
    window->source_count = 0;
    window->source_at = 0;
    window->phase = 0;
    {
        // where the buffers lie, as Horizon sees the memory (audio needs ordinary heap memory)
        MemoryInfo info{};
        u32 page = 0;
        svcQueryMemory(&info, &page, reinterpret_cast<u64>(window->pool));
        fprintf(stderr, "nx: sound: audout at %u Hz, %u channels; buffers at %p: memory type %#x, attributes %#x, "
                        "permissions %#x\n",
                window->out_rate, audoutGetChannelCount(), window->pool, info.type, info.attr, info.perm);
    }

    window->render = render;
    window->user = user;
    window->sound_stop = false;
    // one above the game's thread, on core 1 (the game and the raster are on core 0); it sleeps
    // between looks, so it never starves them
    // a big stack: the mixer's sequences, voices and DSP run on it
    rc = threadCreate(&window->sound_thread, feed, window, nullptr, 0x40000, 0x2a, 1);
    if (R_FAILED(rc)) {
        audoutStopAudioOut();
        audoutExit();
        free(window->pool);
        window->pool = nullptr;
        why = "the sound's thread failed: " + std::to_string(rc);
        return false;
    }
    rc = threadStart(&window->sound_thread);
    if (R_FAILED(rc)) {
        threadClose(&window->sound_thread);
        audoutStopAudioOut();
        audoutExit();
        free(window->pool);
        window->pool = nullptr;
        why = "the sound's thread didn't start: " + std::to_string(rc);
        return false;
    }
    window->sound_open = true;
    fprintf(stderr, "nx: sound open: the game's %u Hz resampled to %u Hz, %d buffers of %d frames\n",
            SA_PC_SOUND_RATE, window->out_rate, PIECES, PIECE);
    return true;
}

void window_sound_close(Window *window)
{
    if (window == nullptr || !window->sound_open)
        return;
    window->sound_stop = true;
    threadWaitForExit(&window->sound_thread);
    threadClose(&window->sound_thread);
    audoutStopAudioOut();
    audoutExit();
    free(window->pool);
    window->pool = nullptr;
    window->sound_open = false;
}

} // namespace pc
