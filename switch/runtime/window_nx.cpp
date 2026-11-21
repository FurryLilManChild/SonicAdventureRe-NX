// SonicAdventureRe-nx: the decomp's window (engine/pc/window.h) on Horizon, in place of its SDL3
// window.cpp. It gives the raster a VI surface on the default NWindow (NVK's VK_NN_vi_surface),
// the pad from libnx's HID, and sound through the audio renderer (audrv) at SA_PC_SOUND_RATE.
//
// The pad is positional, as the Dreamcast's face buttons lie: the Dreamcast's A (bottom) is the
// Switch's B, its B (right) is A, its X (left) is Y and its Y (top) is X. Start is +. ZL and ZR
// (and L and R) are the analog triggers, full or nothing. The left stick is the Dreamcast's stick,
// squared (a stick's round reach stretched to the square a Dreamcast pad reaches), and the d-pad
// moves it too, as the PC build does. Minus and + together end the game.
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

// the sound's pieces: PIECES buffers of PIECE frames each, about 23 ms a piece at 44.1 kHz
constexpr int PIECE = 1024;
constexpr int PIECES = 4;
constexpr size_t PIECE_BYTES = PIECE * 2 * sizeof(int16_t);

const AudioRendererConfig SOUND_CONFIG = {
    AudioRendererOutputRate_48kHz, // output_rate
    1,                             // num_voices
    0,                             // num_effects
    1,                             // num_sinks
    1,                             // num_mix_objs
    2,                             // num_mix_buffers
};

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
    AudioDriver driver;
    void *pool; // the pieces, in one memory pool
    AudioDriverWaveBuf pieces[PIECES];
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

// the sound's thread: each piece the renderer has played is rendered again and queued
void feed(void *arg)
{
    Window *w = static_cast<Window *>(arg);
    while (!w->sound_stop) {
        for (int i = 0; i < PIECES; i++) {
            AudioDriverWaveBuf &p = w->pieces[i];
            if (p.state != AudioDriverWaveBufState_Free && p.state != AudioDriverWaveBufState_Done)
                continue;
            w->render(w->user, p.data_pcm16, PIECE);
            armDCacheFlush(p.data_pcm16, PIECE_BYTES);
            audrvVoiceAddWaveBuf(&w->driver, 0, &p);
        }
        if (!audrvVoiceIsPlaying(&w->driver, 0))
            audrvVoiceStart(&w->driver, 0);
        audrvUpdate(&w->driver);
        audrenWaitFrame();
    }
}

} // namespace

bool window_sound_open(Window *window, SoundRender render, void *user, std::string &why)
{
    if (window == nullptr) {
        why = "no window";
        return false;
    }
    Result rc = audrenInitialize(&SOUND_CONFIG);
    if (R_FAILED(rc)) {
        why = "audrenInitialize failed: " + std::to_string(rc);
        return false;
    }
    rc = audrvCreate(&window->driver, &SOUND_CONFIG, 2);
    if (R_FAILED(rc)) {
        audrenExit();
        why = "audrvCreate failed: " + std::to_string(rc);
        return false;
    }
    const size_t pool_size = (PIECES * PIECE_BYTES + AUDREN_MEMPOOL_ALIGNMENT - 1) & ~(size_t)(AUDREN_MEMPOOL_ALIGNMENT - 1);
    window->pool = memalign(AUDREN_MEMPOOL_ALIGNMENT, pool_size);
    if (window->pool == nullptr) {
        audrvClose(&window->driver);
        audrenExit();
        why = "no memory for the sound's buffers";
        return false;
    }
    memset(window->pool, 0, pool_size);
    armDCacheFlush(window->pool, pool_size);
    int pool = audrvMemPoolAdd(&window->driver, window->pool, pool_size);
    audrvMemPoolAttach(&window->driver, pool);
    static const u8 sink_channels[] = {0, 1};
    audrvDeviceSinkAdd(&window->driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);
    audrvUpdate(&window->driver);
    audrenStartAudioRenderer();

    audrvVoiceInit(&window->driver, 0, 2, PcmFormat_Int16, SA_PC_SOUND_RATE);
    audrvVoiceSetDestinationMix(&window->driver, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&window->driver, 0, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&window->driver, 0, 0.0f, 0, 1);
    audrvVoiceSetMixFactor(&window->driver, 0, 0.0f, 1, 0);
    audrvVoiceSetMixFactor(&window->driver, 0, 1.0f, 1, 1);
    for (int i = 0; i < PIECES; i++) {
        AudioDriverWaveBuf &p = window->pieces[i];
        p = AudioDriverWaveBuf{};
        p.data_pcm16 = reinterpret_cast<s16 *>(static_cast<u8 *>(window->pool) + i * PIECE_BYTES);
        p.size = PIECE_BYTES;
        p.start_sample_offset = 0;
        p.end_sample_offset = PIECE;
    }

    window->render = render;
    window->user = user;
    window->sound_stop = false;
    // one above the game's thread, on core 1 (the game and the raster are on core 0); it waits on
    // the renderer every frame, so it never starves them
    rc = threadCreate(&window->sound_thread, feed, window, nullptr, 0x10000, 0x2a, 1);
    if (R_FAILED(rc)) {
        audrvClose(&window->driver);
        audrenExit();
        free(window->pool);
        window->pool = nullptr;
        why = "the sound's thread failed: " + std::to_string(rc);
        return false;
    }
    threadStart(&window->sound_thread);
    window->sound_open = true;
    fprintf(stderr, "nx: sound open: %u Hz stereo, %d pieces of %d frames\n", SA_PC_SOUND_RATE, PIECES, PIECE);
    return true;
}

void window_sound_close(Window *window)
{
    if (window == nullptr || !window->sound_open)
        return;
    window->sound_stop = true;
    threadWaitForExit(&window->sound_thread);
    threadClose(&window->sound_thread);
    audrvClose(&window->driver);
    audrenExit();
    free(window->pool);
    window->pool = nullptr;
    window->sound_open = false;
}

} // namespace pc
