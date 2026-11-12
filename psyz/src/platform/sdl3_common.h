// Shared SDL3 platform code. Only use via #include.
#ifndef SDL3_COMMON_H
#define SDL3_COMMON_H

#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <libetc.h>
#include <psyz.h>
#include <psyz/overlay.h>
#include <psyz/overlay_sdl3.h>
#include "../internal.h"
#ifdef PLATFORM_IOS
#include "../ios/ios_platform.h"
#endif

// HACK to avoid conflicting with RECT from windef.h
// It renames the libgpu RECT into PS1_RECT, ensuring the windef struct RECT
// does not conflict. The hack is applied to all platform for consistency.
#define RECT PS1_RECT
#include "libgpu.h"
#include "../draw.h"
#undef RECT

/* Display mode 24 reads three consecutive VRAM bytes per output pixel;
 * those bytes are not RGB555 pixels. Keep their 16-bit storage lossless. */
static unsigned char* Sdl3Common_ReadRgb24(int x, int y, int w, int h) {
    if (w <= 0 || w > VRAM_W * 2 / 3 || h <= 0 || x < 0 || x >= VRAM_W ||
        y < 0 || y >= VRAM_H)
        return NULL;
    int words = (w * 3 + 1) / 2;
    if (words > VRAM_W - x || h > VRAM_H - y)
        return NULL;
    u16* raw = calloc((size_t)words * h, sizeof(*raw));
    unsigned char* rgb = malloc((size_t)w * h * 3);
    if (!raw || !rgb) {
        free(raw);
        free(rgb);
        return NULL;
    }
    PS1_RECT region = {x, y, words, h};
    Draw_StoreImage(&region, (u_long*)raw);
    for (int row = 0; row < h; ++row)
        for (int byte = 0; byte < w * 3; ++byte)
            rgb[((size_t)row * w * 3) + byte] =
                (u8)(raw[(size_t)row * words + byte / 2] >> ((byte & 1) * 8));
    free(raw);
    return rgb;
}

/* PS1 field rates: 263 lines of 3413 GPU clocks (NTSC) and 314 lines of 3406
 * (PAL), the GPU clock being the 33.8688 MHz system clock * 715909/451584
 * (NTSC) or * 709379/451584 (PAL). About 59.817 and 49.747 Hz, not the 59.94
 * and 50 Hz of broadcast video: frame-counted game time keeps the original
 * relation to the root counters and the 44100 Hz SPU (music, sequencer). */
#define VSYNC_NTSC (33868800.0 * 715909 / (451584.0 * 3413 * 263))
#define VSYNC_PAL (33868800.0 * 709379 / (451584.0 * 3406 * 314))

// hooks the including renderer backend must implement
static bool PlatformBackend_SetDriverVsync(bool enable);
/* Synced presentation (no tearing) while the frame limiter paces the
 * presents; false when the backend keeps unsynced presents for it. */
static bool PlatformBackend_SetPresentSync(bool enable);
/* Starts the drawing queued for the frame on the GPU before the blank wait
 * (paced presents). */
static void PlatformBackend_SubmitDrawing(void);
/* A present is wanted at a blank that passes without a VSync(0): an
 * interpolated midpoint (Psyz_GpuPrepareInterpolatedBlank). */
static bool PlatformBackend_BlankPresentWanted(void);
/* The present is made at such a blank; the backend sets present_midpoint
 * when it shows an interpolated midpoint (frame log). */
static bool present_blank_poll, present_midpoint;
static Uint32 last_present_blank;
static void PlatformBackend_Present(void);
static void QuitPlatform(void);
#ifdef PLATFORM_WEB
void Psyz_WebWaitForNextFrame(void);
#endif

#ifndef PSYZ_TITLE
#define PSYZ_TITLE "PSY-Z"
#endif
static char window_title[0x100] = {PSYZ_TITLE};
STATIC_ASSERT(sizeof(PSYZ_TITLE) <= sizeof(window_title),
              "PSYZ_TITLE exceeds max allowed characters");

// shared window/platform state
static SDL_Window* sdl3_window = NULL;
static bool is_window_visible = false;
static bool is_platform_initialized = false;
static bool is_platform_init_successful = false;
static SDL_AtomicInt quit_requested = {0};
static SDL_AtomicInt app_in_background = {0};
static SDL_AtomicInt timing_reset_requested = {0};
static SDL_AtomicInt resume_audio_on_foreground = {0};
static bool app_event_watch_installed = false;
static bool debug_show_vram = false;
#ifndef PLATFORM_IOS
static bool is_fullscreen = false;
#endif

// defaults to better support integer scaling
#define DEFAULT_FRONT_W 1280
#define DEFAULT_FRONT_H 960

typedef struct {
    int w, h;
} WndSize;

static inline u8 color_5to8(unsigned int c5) {
    return (u8)((c5 * 255 + 15) / 31);
}
static inline unsigned int color_8to5(unsigned int c8) {
    return (c8 * 31 + 127) / 255;
}
static void ConvertRgb5551ToRgba8888(const u16* src, u8* dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        u16 v = src[i];
        dst[0] = color_5to8(v & 0x1F);
        dst[1] = color_5to8((v >> 5) & 0x1F);
        dst[2] = color_5to8((v >> 10) & 0x1F);
        dst[3] = (v & 0x8000) ? 0xFF : 0x00;
        dst += 4;
    }
}
static void ConvertRgba8888ToRgb5551(const u8* src, u16* dst, size_t pixels) {
    for (size_t i = 0; i < pixels; i++) {
        u16 v = (u16)color_8to5(src[0]);
        v |= (u16)(color_8to5(src[1]) << 5);
        v |= (u16)(color_8to5(src[2]) << 10);
        v |= (src[3] >= 0x80) ? 0x8000 : 0;
        dst[i] = v;
        src += 4;
    }
}

static inline bool RectsOverlap(
    int src_x, int src_y, int dst_x, int dst_y, int w, int h) {
    return src_x < dst_x + w && dst_x < src_x + w && src_y < dst_y + h &&
           dst_y < src_y + h;
}

static void* vram_convert_buf = NULL;
static size_t vram_convert_cap = 0;
static void* GetVramConvertBuffer(size_t size) {
    if (vram_convert_cap < size) {
        void* p = realloc(vram_convert_buf, size);
        if (!p) {
            ERRORF("failed to allocate %zu bytes", size);
            return NULL;
        }
        vram_convert_buf = p;
        vram_convert_cap = size;
    }
    return vram_convert_buf;
}

static bool disp_on = false;
static int set_disp_horiz = 256;
static int set_disp_vert = 240;
static int cur_disp_horiz = -1;
static int cur_disp_vert = -1;
static bool is_pal = false;
static PsyzAspectMode aspect_mode = PSYZ_ASPECT_DISPLAY;
static WndSize wnd_size_in_pixels = {0, 0};

static void SetWindowSizeInPixels(int width, int height) {
    if (!sdl3_window) {
        return;
    }
    wnd_size_in_pixels.w = width;
    wnd_size_in_pixels.h = height;
    float density = SDL_GetWindowPixelDensity(sdl3_window);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    int actual_width = (int)(width / density + 0.5f);
    int actual_height = (int)(height / density + 0.5f);
    SDL_SetWindowSize(sdl3_window, actual_width, actual_height);
}

static float GetCurrentGameAspectRatio(int disp_w, int disp_h) {
    if (aspect_mode == PSYZ_ASPECT_DISPLAY) {
        float vref = is_pal || Psyz_GpuGetNtscPalPicture() ? 288.0f : 240.0f;
        return (4.0f / 3.0f) * ((float)set_disp_horiz / 256.0f) /
               ((float)set_disp_vert / vref);
    }
    if (disp_h <= 0) {
        return 4.0f / 3.0f;
    }
    return (float)disp_w / (float)disp_h;
}

// Fit a game of the given aspect ratio into the window, centering it and
// leaving black bars on the axis that does not fill.
static SDL_Rect FitGameToWindow(float game_aspect, WndSize win) {
    SDL_Rect r;
    if (game_aspect <= 0.0 || win.w <= 0 || win.h <= 0) {
        r.x = 0;
        r.y = 0;
        r.w = win.w;
        r.h = win.h;
        return r;
    }
    float window_aspect = (float)win.w / (float)win.h;
    if (window_aspect > game_aspect) {
        r.h = win.h;
        r.w = (int)(win.h * game_aspect + 0.5f);
    } else {
        r.w = win.w;
        r.h = (int)(win.w / game_aspect + 0.5f);
    }
    r.x = (win.w - r.w) / 2;
    r.y = (win.h - r.h) / 2;
    return r;
}

int Psyz_VideoSetAspectMode(PsyzAspectMode mode) {
    if (mode != PSYZ_ASPECT_DISPLAY && mode != PSYZ_ASPECT_SQUARE) {
        return -1;
    }
#ifdef PE_PS5_GPU_NATIVE
    /* Finish queued vertices with their old aspect-dependent draw contract. */
    if(aspect_mode!=mode)Draw_FlushBuffer();
#endif
    aspect_mode = mode;
    return 0;
}

PsyzAspectMode Psyz_VideoGetAspectMode(void) { return aspect_mode; }

static Uint32 last_vsync_us = 0; // time of the last blocking Psyz_VideoVSync
/* The blank the frame limiter waited for (WaitForNextFrame). */
static Uint32 wall_blank_us;
static bool wall_blank_valid;
static Uint32 vblank_clock_offset;
static unsigned host_clock_pause_depth;
static Uint32 host_clock_pause_count;
static Uint32 limitless_vblank_base;
static Uint64 limitless_frame_base;
/* Without pacing no time passes between blanks, so VSync(1) counts the
 * scanlines a polling loop would have waited: each query advances one line
 * and a blocking VSync(0) skips the fields those lines crossed. */
static Uint32 limitless_lines;

// frame pacing state
static double target_frame_rate = VSYNC_NTSC;
static double target_frame_time_us = 1000000.0 / VSYNC_NTSC;
static Uint64 perf_frequency = 0;
static Uint64 last_frame_time = 0;
static Uint64 finish_time = 0;
static double drift_compensation = 0.0;
static PsyzVsyncMode vsync_mode = PSYZ_VSYNC_AUTO;
static PsyzDitherMode dither_mode = PSYZ_DITHER_AUTO;
static bool use_driver_vsync = false;
/* The frame limiter paces synced presents (a display faster than the
 * fields, e.g. 120 Hz): no tearing, and adaptive sync in fullscreen shows
 * each field when it is presented. */
static bool present_sync = false;
/* Fullscreen switches the display to a mode at the field rate (50 Hz for
 * PAL) when it has one (Psyz_VideoSetFullscreenFieldRate). */
static bool fullscreen_field_rate = false;
static PsyzVideoStats gpu_stats = {0};

// game speed (Psyz_VideoSetSpeed): the VSync mode is never changed for it
static unsigned game_speed = 1;
static Uint64 last_present_time = 0;
static bool present_skip = false; // this frame is not shown
static bool frame_presented = false; // swapchain image was acquired and queued this frame

static Uint32 WallVblankCount(void) {
    return (Uint32)((double)SDL_GetTicksNS() * target_frame_rate * game_speed /
                    1e9);
}

static void PollEvents(void);

// overlay callbacks shared across backends
static PsyzOverlayEventFilter_SDL3 overlay_event_filter;
PsyzOverlayEventFilter_SDL3 Psyz_SetOverlayEventFilter_SDL3(
    PsyzOverlayEventFilter_SDL3 filter) {
    PsyzOverlayEventFilter_SDL3 previous = overlay_event_filter;
    overlay_event_filter = filter;
    return previous;
}
static PsyzOverlayEventCB_SDL3 overlay_event_cb;
PsyzOverlayEventCB_SDL3 Psyz_OverlayEvent_SDL3(PsyzOverlayEventCB_SDL3 cb) {
    const PsyzOverlayEventCB_SDL3 prev = overlay_event_cb;
    overlay_event_cb = cb;
    return prev;
}

static PsyzOverlayFrameCB overlay_frame_cb;
PsyzOverlayFrameCB Psyz_OverlayFrameCB(PsyzOverlayFrameCB cb) {
    const PsyzOverlayFrameCB prev = overlay_frame_cb;
    overlay_frame_cb = cb;
    return prev;
}

static PsyzOverlayDestroyCB overlay_destroy_cb;
PsyzOverlayDestroyCB Psyz_OverlayDestroyCB(PsyzOverlayDestroyCB cb) {
    const PsyzOverlayDestroyCB prev = overlay_destroy_cb;
    overlay_destroy_cb = cb;
    return prev;
}

void Psyz_SetTitle(const char* str) {
    strncpy(window_title, str, sizeof(window_title) - 1);
    window_title[sizeof(window_title) - 1] = 0;
    if (sdl3_window) {
        SDL_SetWindowTitle(sdl3_window, window_title);
    }
}

PsyzSize Psyz_VideoGetDisplaySize(void) {
    PsyzSize s = {0, 0};
    if (sdl3_window) {
        SDL_GetWindowSizeInPixels(sdl3_window, &s.w, &s.h);
    }
    return s;
}

void Psyz_VideoSetDrawArea(PsyzRect rect) {
    // not usable on SDL3, consoles only
    (void)rect;
}

static double GetElapsedMicroseconds(Uint64 start, Uint64 end) {
    return ((double)(end - start) * 1000000.0) / (double)perf_frequency;
}

/* The display the window is on (the primary one before there is a window). */
static SDL_DisplayID WindowDisplay(void) {
    SDL_DisplayID display = sdl3_window ? SDL_GetDisplayForWindow(sdl3_window) : 0;
    return display ? display : SDL_GetPrimaryDisplay();
}

static void ConfigureVSync(double target_fps) {
    present_sync = false;
    if (vsync_mode == PSYZ_VSYNC_ON) {
        use_driver_vsync = PlatformBackend_SetDriverVsync(true);
        INFOF("vsync forced ON (driver VSync)");
        return;
    }
    if (vsync_mode == PSYZ_VSYNC_OFF) {
        PlatformBackend_SetDriverVsync(false);
        use_driver_vsync = false;
        INFOF("vsync forced OFF (frame limiter)");
        return;
    }
    if (vsync_mode == PSYZ_VSYNC_LIMITLESS) {
        PlatformBackend_SetDriverVsync(false);
        use_driver_vsync = false;
        INFOF("vsync forced OFF, internal frame limiter disabled");
        return;
    }

    SDL_DisplayID display_id = WindowDisplay();
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display_id);
    double detected_framerate = 60.0;
    if (mode && mode->refresh_rate > 0.0f) {
        detected_framerate = mode->refresh_rate;
    }

    const double target_time = 1.0 / target_fps;
    const double tolerance_us = target_time * 0.05;
    bool can_use_driver_vsync =
        fabs((1.0 / detected_framerate) - target_time) < tolerance_us;

    if (can_use_driver_vsync && PlatformBackend_SetDriverVsync(true)) {
        use_driver_vsync = true;
        INFOF("detected %.2f Hz monitor, use driver VSync", detected_framerate);
    } else if (detected_framerate > target_fps * 1.05 &&
               PlatformBackend_SetPresentSync(true)) {
        use_driver_vsync = false;
        present_sync = true;
        INFOF("detected %.2f Hz monitor, frame limiter paces %.2f Hz synced "
              "presents", detected_framerate, target_fps);
    } else {
        PlatformBackend_SetDriverVsync(false);
        use_driver_vsync = false;
        INFOF("detected %.2f Hz monitor but targeting %.2f, use frame limiter",
              detected_framerate, target_fps);
    }
}

static void ApplyFullscreenMode(void);
static void UpdateTargetFramerate(double fps) {
    const bool changed = fps != target_frame_rate;
    target_frame_rate = fps;
    target_frame_time_us = 1000000.0 / fps;
    drift_compensation = 0.0;
    last_frame_time = SDL_GetPerformanceCounter();
#ifndef PLATFORM_IOS
    if (changed && fullscreen_field_rate)
        ApplyFullscreenMode(); /* the mode at the new field rate */
#else
    (void)changed;
#endif
    ConfigureVSync(target_frame_rate);
}

static bool SDLCALL Sdl3Common_AppEventWatch(void* userdata, SDL_Event* event) {
    (void)userdata;
    switch (event->type) {
    case SDL_EVENT_TERMINATING:
        SDL_SetAtomicInt(&quit_requested, 1);
        Psyz_AudioPause();
#ifdef PLATFORM_IOS
        Psyz_IosResetTouchControls();
#endif
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        SDL_SetAtomicInt(
            &resume_audio_on_foreground, Psyz_AudioIsPaused() ? 0 : 1);
        SDL_SetAtomicInt(&app_in_background, 1);
        Psyz_AudioPause();
#ifdef PLATFORM_IOS
        Psyz_IosResetTouchControls();
#endif
        break;
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        if (!SDL_GetAtomicInt(&app_in_background)) {
            SDL_SetAtomicInt(
                &resume_audio_on_foreground, Psyz_AudioIsPaused() ? 0 : 1);
        }
        SDL_SetAtomicInt(&app_in_background, 1);
        Psyz_AudioPause();
#ifdef PLATFORM_IOS
        Psyz_IosResetTouchControls();
#endif
        break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        SDL_SetAtomicInt(&app_in_background, 0);
        SDL_SetAtomicInt(&timing_reset_requested, 1);
        if (SDL_SetAtomicInt(&resume_audio_on_foreground, 0)) {
            Psyz_AudioUnpause();
        }
        break;
    default:
        break;
    }
    return true;
}

static void Sdl3Common_InstallAppEventWatch(void) {
    if (!app_event_watch_installed) {
        if (SDL_AddEventWatch(Sdl3Common_AppEventWatch, NULL)) {
            app_event_watch_installed = true;
        } else {
            WARNF("failed to install SDL lifecycle event watch: %s",
                  SDL_GetError());
        }
    }
}

static void Sdl3Common_ApplyPendingTimingReset(void) {
    if (!SDL_GetAtomicInt(&timing_reset_requested)) {
        return;
    }
    SDL_SetAtomicInt(&timing_reset_requested, 0);
    drift_compensation = 0.0;
    last_frame_time = SDL_GetPerformanceCounter();
    finish_time = last_frame_time;
    last_vsync_us = (Uint32)(SDL_GetTicksNS() / 1000);
    if (sdl3_window && is_platform_init_successful) {
        ConfigureVSync(target_frame_rate);
    }
}

static bool Sdl3Common_IsInBackground(void) {
    return SDL_GetAtomicInt(&app_in_background) != 0;
}

int Psyz_QuitRequested(void) { return SDL_GetAtomicInt(&quit_requested) != 0; }

// Initialize the timing/vsync state. Call once at the end of InitPlatform.
static void Sdl3Common_TimingInit(void) {
    last_vsync_us = (Uint32)(SDL_GetTicksNS() / 1000);
    perf_frequency = SDL_GetPerformanceFrequency();
    last_frame_time = SDL_GetPerformanceCounter();
    finish_time = last_frame_time;
    SDL_SetAtomicInt(&quit_requested, 0);
    SDL_SetAtomicInt(&app_in_background, 0);
    SDL_SetAtomicInt(&timing_reset_requested, 0);
    SDL_SetAtomicInt(&resume_audio_on_foreground, 0);
    Sdl3Common_InstallAppEventWatch();
    UpdateTargetFramerate(VSYNC_NTSC);
}

static void Sdl3Common_Shutdown(void) {
    if (app_event_watch_installed) {
        SDL_RemoveEventWatch(Sdl3Common_AppEventWatch, NULL);
        app_event_watch_installed = false;
    }
#ifdef PLATFORM_IOS
    Psyz_IosDetachWindow();
#endif
    SDL_SetAtomicInt(&app_in_background, 0);
    SDL_SetAtomicInt(&timing_reset_requested, 0);
    SDL_SetAtomicInt(&resume_audio_on_foreground, 0);
}

static void WaitForNextFrame(void) {
    Uint64 current_time = SDL_GetPerformanceCounter();
    double elapsed_us = GetElapsedMicroseconds(last_frame_time, current_time);

#ifdef PLATFORM_WEB
    // yield to browser
    (void)elapsed_us;
    Psyz_WebWaitForNextFrame();
#else
    /* Above normal speed driver VSync no longer paces every frame (most are
     * not shown), so the limiter does, at the faster rate. Also fall back to
     * software pacing when no swapchain image was presented (e.g. minimized). */
    if ((!use_driver_vsync || !frame_presented || game_speed > 1) &&
        vsync_mode != PSYZ_VSYNC_LIMITLESS) {
        /* Wait for the next blank of the wall clock that counts the blanks
         * (WallVblankCount), as the console waits for the next vertical
         * blank: the fields keep one phase, a frame that overran a field
         * waits for the following blank rather than starting at once, and
         * VSync(1) counts lines from that blank. */
        (void)elapsed_us;
        const double field_ns = 1e9 / (target_frame_rate * game_speed);
        const Uint64 now_ns = SDL_GetTicksNS();
        const Uint64 blank_ns =
            (Uint64)((floor((double)now_ns / field_ns) + 1.0) * field_ns);
        /* Sleep to a margin before the blank and spin the rest. The margin
         * follows the sleeps' recent overshoot (the system's timer slack,
         * often over 1 ms on macOS), so the wait ends at the blank. */
        static double sleep_margin_ns = 1000000.0;
        const Uint64 margin = (Uint64)sleep_margin_ns;
        if (blank_ns > now_ns + margin) {
            const Uint64 wake = blank_ns - margin;
            SDL_DelayNS(wake - now_ns);
            const double over = (double)(Sint64)(SDL_GetTicksNS() - wake);
            sleep_margin_ns = SDL_clamp(
                SDL_max(sleep_margin_ns * 0.99, over + 250000.0), 500000.0,
                4000000.0);
        }
        while (SDL_GetTicksNS() < blank_ns) {
        }
        wall_blank_us = (Uint32)(blank_ns / 1000);
        wall_blank_valid = true;
    }
#endif

    Uint64 frame_end_time = SDL_GetPerformanceCounter();
    gpu_stats.last_frame_time_us =
        GetElapsedMicroseconds(last_frame_time, frame_end_time);
    gpu_stats.last_draw_time_us =
        GetElapsedMicroseconds(last_frame_time, finish_time);
    gpu_stats.target_frame_time_us = target_frame_time_us / game_speed;
    gpu_stats.total_frames++;
    gpu_stats.using_driver_vsync = use_driver_vsync;

    last_frame_time = frame_end_time;
}

/* Frame timing statistics (PSYZ_FRAME_STATS=1 prints a summary every 500
 * presents, =n every n; PSYZ_FRAME_LOG=file writes one line per present). Each blocking
 * VSync is split into the application's CPU time since the previous VSync
 * returned ("work"), the present call ("present") and the pacing wait; the
 * interval is the time between consecutive present calls. after_blank_ms is
 * the time from the blank to a paced present (PresentDue; 0 when presents
 * are not paced). */
#define FRAME_STATS_WINDOW 500
static int frame_stats_mode = -1;
static FILE* frame_stats_log;
static Uint64 frame_stats_last_present, frame_stats_last_return;
static unsigned frame_stats_count, frame_stats_window;
static float frame_stats_interval[FRAME_STATS_WINDOW];
static float frame_stats_work[FRAME_STATS_WINDOW];
static float frame_stats_present[FRAME_STATS_WINDOW];
/* Render passes the backend drew primitives in since the last present. */
static unsigned frame_stats_batches;
static float frame_stats_batch[FRAME_STATS_WINDOW];

static int FrameStatsCompare(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}

static void FrameStatsSummary(const char* name, float* v, unsigned n) {
    double sum = 0;
    qsort(v, n, sizeof(*v), FrameStatsCompare);
    for (unsigned i = 0; i < n; i++)
        sum += v[i];
    INFOF("[Frame stats] %-10s mean %.2f p50 %.2f p90 %.2f p99 %.2f max %.2f"
          " min %.2f",
          name, sum / n, v[n / 2], v[n * 9 / 10], v[n * 99 / 100], v[n - 1],
          v[0]);
}

/* From the end of the blank wait to a paced present (after_blank_ms). */
static float present_after_blank_ms;

static void FrameStatsRecord(Uint64 work_end, Uint64 present_start,
                             Uint64 present_end, Uint64 wait_start,
                             Uint64 wait_end) {
    if (frame_stats_mode < 0) {
        const char* stats = SDL_getenv("PSYZ_FRAME_STATS");
        const char* log = SDL_getenv("PSYZ_FRAME_LOG");
        frame_stats_mode = stats && *stats && strcmp(stats, "0");
        /* PSYZ_FRAME_STATS=n summarises every n presents (up to 500). */
        frame_stats_window = stats ? (unsigned)atoi(stats) : 0;
        if (frame_stats_window < 10 || frame_stats_window > FRAME_STATS_WINDOW)
            frame_stats_window = FRAME_STATS_WINDOW;
        if (log && *log && (frame_stats_log = fopen(log, "w")))
            fprintf(frame_stats_log, "# present_ms interval_ms work_ms "
                                     "present_call_ms wait_ms passes "
                                     "after_blank_ms midpoint\n");
    }
    if (!frame_stats_mode && !frame_stats_log)
        return;
    if (frame_stats_last_present) {
        float interval = (float)(GetElapsedMicroseconds(
                                     frame_stats_last_present, present_start) /
                                 1000.0);
        float work = (float)(GetElapsedMicroseconds(frame_stats_last_return,
                                                    work_end) /
                             1000.0);
        float present =
            (float)(GetElapsedMicroseconds(present_start, present_end) / 1000.0);
        float wait =
            (float)(GetElapsedMicroseconds(wait_start, wait_end) / 1000.0);
        if (frame_stats_log)
            fprintf(frame_stats_log, "%.3f %.3f %.3f %.3f %.3f %u %.3f %d\n",
                    GetElapsedMicroseconds(0, present_start) / 1000.0,
                    interval, work, present, wait, frame_stats_batches,
                    present_after_blank_ms, present_midpoint);
        if (frame_stats_mode) {
            frame_stats_interval[frame_stats_count] = interval;
            frame_stats_work[frame_stats_count] = work;
            frame_stats_present[frame_stats_count] = present;
            frame_stats_batch[frame_stats_count] = (float)frame_stats_batches;
            if (++frame_stats_count == frame_stats_window) {
                INFOF("[Frame stats] %u presents, target %.3f ms, driver vsync %d",
                      frame_stats_count, target_frame_time_us / game_speed / 1000.0,
                      use_driver_vsync);
                FrameStatsSummary("interval_ms", frame_stats_interval,
                                  frame_stats_count);
                FrameStatsSummary("work_ms", frame_stats_work, frame_stats_count);
                FrameStatsSummary("present_ms", frame_stats_present,
                                  frame_stats_count);
                FrameStatsSummary("passes", frame_stats_batch,
                                  frame_stats_count);
                frame_stats_count = 0;
            }
        }
    }
    frame_stats_last_present = present_start;
    frame_stats_last_return = wait_end;
    frame_stats_batches = 0;
}

/* Paced presentation (frame limiter): VSync(0) submits the frame's drawing
 * and waits for the blank, and the present follows at the game's next
 * display change (PutDispEnv after the blank) or its next drawing, whatever
 * comes first (PresentDue), else at its next VSync. The field the game shows
 * from that blank is presented at it, as evenly spaced as the blanks,
 * instead of whenever the frame's drawing ends: a variable refresh (adaptive
 * sync) display shows every field for the same time, a faster fixed-rate one
 * in a steady pattern of refreshes, and the picture changes at the blank as
 * on the console. PSYZ_PRESENT_PACING=0 presents in VSync before the wait. */
static int present_pacing = -1;
static bool present_due;
static Uint64 due_work_end, due_wait_start, due_wait_end;
static bool SkipPresent(void);

static bool PacedPresents(void) {
    if (present_pacing < 0) {
        const char* env = SDL_getenv("PSYZ_PRESENT_PACING");
        present_pacing = !(env && !strcmp(env, "0"));
    }
#ifdef PLATFORM_WEB
    return false;
#else
    return present_pacing && !use_driver_vsync && game_speed == 1 &&
           vsync_mode != PSYZ_VSYNC_LIMITLESS && !host_clock_pause_depth;
#endif
}

/* Presents; returns when the present call started and ended. */
static void PresentFrame(Uint64* present_start, Uint64* present_end) {
    last_present_blank = WallVblankCount();
    *present_start = SDL_GetPerformanceCounter();
    present_skip = SkipPresent();
    PlatformBackend_Present();
    present_skip = false;
    *present_end = SDL_GetPerformanceCounter();
}

/* The backend calls this before a display change or drawing. */
static void PresentDue(void) {
    if (!present_due)
        return;
    present_due = false;
    present_after_blank_ms = (float)(GetElapsedMicroseconds(
                                         due_wait_end,
                                         SDL_GetPerformanceCounter()) /
                                     1000.0);
    Uint64 present_start, present_end;
    PresentFrame(&present_start, &present_end);
    FrameStatsRecord(due_work_end, present_start, present_end, due_wait_start,
                     due_wait_end);
    present_midpoint = false;
    present_after_blank_ms = 0.0f;
}

/* Implemented by the existing SDL audio backend; no device in video-clock mode.
 */
void Sdl3Audio_VBlank(bool pal);
void Sdl3Audio_SetSpeed(unsigned speed);

/* Above normal speed with driver VSync, show a frame only once most of a
 * refresh has passed since the last one shown: presents never wait on a
 * full swapchain queue and the newest frame is the one on screen. */
static bool SkipPresent(void) {
    if (game_speed < 2 || (!use_driver_vsync && !present_sync)) {
        return false;
    }
    Uint64 now = SDL_GetPerformanceCounter();
    if (last_present_time && GetElapsedMicroseconds(last_present_time, now) <
                                 target_frame_time_us * 0.75) {
        return true;
    }
    last_present_time = now;
    return false;
}

/* Offline clock (PSYZ_AUDIO_CLOCK=video) without pacing: the emulated time
 * of the last blocking VSync, from which VSync(1) counts lines. */
static uint64_t offline_vsync_us;

static double LineMicroseconds(void) {
    return is_pal ? 1000000.0 / (VSYNC_PAL * 314) : 1000000.0 / (VSYNC_NTSC * 263);
}

void Psyz_VideoWaitBlank(void) {
    if (vsync_mode != PSYZ_VSYNC_LIMITLESS || host_clock_pause_depth)
        return;
    limitless_vblank_base++;
    Psyz_VSyncBlank();
}

int Psyz_VideoVSync(int mode) {
    Uint32 now_us;
    unsigned short ret;
    unsigned blanks = 1;
    Sdl3Common_ApplyPendingTimingReset();
    if (mode < 0) {
        if (host_clock_pause_depth)
            return (int)host_clock_pause_count;
        /* With pacing disabled, each explicit present advances one blank.
         * Wall time cannot drive this counter in an accelerated runner. */
        if (vsync_mode == PSYZ_VSYNC_LIMITLESS)
            return (
                int)(limitless_vblank_base +
                     (Uint32)(gpu_stats.total_frames - limitless_frame_base));
        return (int)(WallVblankCount() + vblank_clock_offset);
    }
    if (vsync_mode == PSYZ_VSYNC_LIMITLESS && !host_clock_pause_depth) {
        /* On the offline clock each query spends a line of emulated time, as
         * a polling loop would: blanks it crosses happen in that wait
         * (Psyz_VideoWaitBlank). The count runs from the last VSync(0). */
        int offline = Psyz_AudioWaitUs(mode == 1 ? 64 : 0);
        if (offline) {
            ret = (unsigned short)(
                (double)(Psyz_KernelNowUs() - offline_vsync_us) /
                LineMicroseconds());
            if (mode == 1)
                return ret;
        } else {
            if (mode == 1)
                return (int)(unsigned short)++limitless_lines;
            /* A game that waits on VSync(1) past the end of a field (e.g.
             * a 384-line threshold for a 2-field frame) misses that blank. */
            blanks += limitless_lines / (is_pal ? 314u : 263u);
            limitless_vblank_base += limitless_lines / (is_pal ? 314u : 263u);
            ret = (unsigned short)limitless_lines;
        }
        limitless_lines = 0;
    } else {
        // a line is approximated as 64 us of emulated (speed-scaled) time
        now_us = (Uint32)(SDL_GetTicksNS() / 1000);
        ret = (unsigned short)(((Uint64)(Uint32)(now_us - last_vsync_us) *
                                game_speed) >> 6);
    }
    /* A game polling VSync(1) across a blank: a midpoint wanted there. */
    if (mode == 1 && !present_due && PacedPresents() &&
        PlatformBackend_BlankPresentWanted() &&
        WallVblankCount() != last_present_blank) {
        const Uint64 last_return = frame_stats_last_return;
        Uint64 present_start, present_end;
        present_blank_poll = true;
        PresentFrame(&present_start, &present_end);
        present_blank_poll = false;
        FrameStatsRecord(present_start, present_start, present_end,
                         present_end, present_end);
        frame_stats_last_return = last_return; /* work goes on */
        present_midpoint = false;
    }
    if (mode == 0) {
        const Uint64 work_end = SDL_GetPerformanceCounter();
        PresentDue(); /* none came since the last blank */
        if (PacedPresents()) {
            PlatformBackend_SubmitDrawing();
            PollEvents();
            due_work_end = work_end;
            due_wait_start = SDL_GetPerformanceCounter();
            WaitForNextFrame();
            due_wait_end = SDL_GetPerformanceCounter();
            present_due = true;
        } else {
            Uint64 present_start, present_end;
            PresentFrame(&present_start, &present_end);
            PollEvents();
            WaitForNextFrame();
            FrameStatsRecord(work_end, present_start, present_end, present_end,
                             SDL_GetPerformanceCounter());
            present_midpoint = false;
        }
        /* The offline audio clock follows every blank the counter advanced
         * by, including those a VSync(1) wait spent before this present
         * (a 2-field frame). */
        while (blanks--)
            Sdl3Audio_VBlank(is_pal);
        offline_vsync_us = Psyz_KernelNowUs();
        last_vsync_us = wall_blank_valid ? wall_blank_us
                                         : (Uint32)(SDL_GetTicksNS() / 1000);
        wall_blank_valid = false;
    }
    return ret;
}

void Psyz_PauseHostClock_SDL3(bool paused) {
    if (paused) {
        if (!host_clock_pause_depth) {
            host_clock_pause_count = (Uint32)Psyz_VideoVSync(-1);
            Psyz_KernelPauseHostClock(1);
        }
        ++host_clock_pause_depth;
    } else if (host_clock_pause_depth && !--host_clock_pause_depth) {
        Psyz_KernelPauseHostClock(0);
        if (vsync_mode == PSYZ_VSYNC_LIMITLESS) {
            limitless_vblank_base = host_clock_pause_count;
            limitless_frame_base = gpu_stats.total_frames;
        } else {
            vblank_clock_offset = host_clock_pause_count - WallVblankCount();
        }
        drift_compensation = 0.0;
        last_frame_time = SDL_GetPerformanceCounter();
        finish_time = last_frame_time;
        last_vsync_us = (Uint32)(SDL_GetTicksNS() / 1000);
    }
}

int Psyz_VideoSetVsyncMode(PsyzVsyncMode mode) {
    if (mode < 0 || mode > 3) {
        return -1;
    }
    Uint32 previous_count = (Uint32)Psyz_VideoVSync(-1);
    vsync_mode = mode;
    if (mode == PSYZ_VSYNC_LIMITLESS) {
        limitless_vblank_base = previous_count;
        limitless_frame_base = gpu_stats.total_frames;
    } else {
        vblank_clock_offset = previous_count - WallVblankCount();
    }
    if (sdl3_window && is_platform_init_successful) {
        ConfigureVSync(target_frame_rate);
    }
    return 0;
}

PsyzVsyncMode Psyz_VideoGetVsyncMode(void) { return vsync_mode; }

int Psyz_VideoSetSpeed(unsigned speed) {
    if (speed < 1 || speed > PSYZ_SPEED_MAX) {
        return -1;
    }
    if (speed == game_speed) {
        return 0;
    }
    // the blank count carries on from where it is, at the new rate
    Uint32 previous_count = (Uint32)Psyz_VideoVSync(-1);
    game_speed = speed;
    if (vsync_mode != PSYZ_VSYNC_LIMITLESS && !host_clock_pause_depth) {
        vblank_clock_offset = previous_count - WallVblankCount();
    }
    Psyz_KernelSetHostClockSpeed(speed);
    Sdl3Audio_SetSpeed(speed);
    drift_compensation = 0.0;
    last_frame_time = SDL_GetPerformanceCounter();
    last_present_time = 0;
    return 0;
}

unsigned Psyz_VideoGetSpeed(void) { return game_speed; }

static int s_dither = 0;
static inline int GetCurrentDither(void) {
    if (dither_mode == PSYZ_DITHER_OFF) {
        return 0;
    }
    return s_dither;
}

static inline void SetDither(int dither) { s_dither = dither; }

/* Gouraud shading or a modulated texture dithers; a raw texture never does,
 * not even with Gouraud colours (which it ignores). */
static inline int CanPolyDither(int isGouraud, int isTextured, int isShadeTex) {
    return GetCurrentDither() && isShadeTex && (isGouraud || isTextured);
}

static int CanLineDither(void) { return GetCurrentDither(); }

int Psyz_VideoSetDitheringMode(PsyzDitherMode mode) {
    if (mode != PSYZ_DITHER_AUTO && mode != PSYZ_DITHER_OFF) {
        return -1;
    }
    if (dither_mode == mode) {
        return 0;
    }
    /* Off also changes color precision, even when the draw env disables dither.
     * Before the platform is up nothing is queued, and flushing would start
     * it early. */
    if (is_platform_init_successful)
        Draw_FlushBuffer();
    dither_mode = mode;
    return 0;
}

PsyzDitherMode Psyz_VideoGetDitheringMode(void) { return dither_mode; }

int Psyz_VideoStats(PsyzVideoStats* stats) {
    if (!stats || !is_platform_init_successful) {
        return -1;
    }
    *stats = gpu_stats;
    return 0;
}

struct Gamepad {
    SDL_JoystickID id;
    SDL_Gamepad* dev;
    const char* name;
};
static bool is_pads_init = false;
static struct Gamepad gamepads[16] = {0};

static void UpdatePlatformTouchControls(void) {
#ifdef PLATFORM_IOS
    Psyz_IosSetTouchControlsVisible(is_pads_init && !gamepads[0].dev);
#endif
}

static void AddGamepad(SDL_JoystickID id) {
    int i;
    for (i = 0; i < LEN(gamepads); i++) {
        if (gamepads[i].id == id) {
            return;
        }
    }
    for (i = 0; i < LEN(gamepads); i++) {
        if (!gamepads[i].id) {
            break;
        }
    }
    if (i == LEN(gamepads)) {
        ERRORF("too many gamepads connected");
        return;
    }

    SDL_Gamepad* dev = SDL_OpenGamepad(id);
    if (!dev) {
        ERRORF("failed to open gamepad %d", (int)id);
        return;
    }
    gamepads[i].id = id;
    gamepads[i].dev = dev;
    gamepads[i].name = SDL_GetGamepadName(dev);
    INFOF("connected %s", gamepads[i].name);
    UpdatePlatformTouchControls();
}

static void RemoveGamepad(SDL_JoystickID id) {
    int i;
    for (i = 0; i < LEN(gamepads); i++) {
        if (gamepads[i].id == id) {
            break;
        }
    }
    if (i == LEN(gamepads)) {
        WARNF("gamepad already removed");
        return;
    }

    SDL_CloseGamepad(gamepads[i].dev);
    for (; i + 1 < LEN(gamepads); i++) {
        gamepads[i] = gamepads[i + 1];
    }
    gamepads[LEN(gamepads) - 1] = (struct Gamepad){0};
    UpdatePlatformTouchControls();
}

void MyPadInit(int mode) {
    (void)mode;
    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            ERRORF("failed to initialize SDL_INIT_GAMEPAD");
            return;
        }
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; i++) {
            AddGamepad(ids[i]);
        }
        SDL_free(ids);
    }
    is_pads_init = true;
    UpdatePlatformTouchControls();
}

static u_long keyb_p1[] = {
    SDL_SCANCODE_W,         // PAD_L2
    SDL_SCANCODE_E,         // PAD_R2
    SDL_SCANCODE_Q,         // PAD_L1
    SDL_SCANCODE_R,         // PAD_R1
    SDL_SCANCODE_S,         // PAD_TRIANGLE
    SDL_SCANCODE_D,         // PAD_CIRCLE
    SDL_SCANCODE_X,         // PAD_CROSS
    SDL_SCANCODE_Z,         // PAD_SQUARE
    SDL_SCANCODE_BACKSPACE, // PAD_SELECT
    SDL_SCANCODE_1,         // PAD_L3
    SDL_SCANCODE_2,         // PAD_R3
    SDL_SCANCODE_RETURN,    // PAD_START
    SDL_SCANCODE_UP,        // PAD_UP
    SDL_SCANCODE_RIGHT,     // PAD_RIGHT
    SDL_SCANCODE_DOWN,      // PAD_DOWN
    SDL_SCANCODE_LEFT,      // PAD_LEFT
};
static unsigned int PadRead_Keyboard(u_long* config, int config_len) {
    const bool* keyb = SDL_GetKeyboardState(NULL);
    unsigned int r = 0;
    for (int i = 0; i < config_len; i++) {
        if (keyb[config[i]]) {
            r |= 1UL << i;
        }
    }
    return r;
}
static int gamepad_p1[16] = {
    SDL_GAMEPAD_BUTTON_COUNT,         SDL_GAMEPAD_BUTTON_COUNT + 1,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_NORTH,         SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_BACK,          SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,   SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_DPAD_UP,       SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,     SDL_GAMEPAD_BUTTON_DPAD_LEFT};
int Psyz_GetKeyboardBinding_SDL3(unsigned button) {
    return button < 16 ? (int)keyb_p1[button] : -1;
}
int Psyz_SetKeyboardBinding_SDL3(unsigned button, SDL_Scancode key) {
    if (button >= 16 || key <= SDL_SCANCODE_UNKNOWN ||
        key >= SDL_SCANCODE_COUNT || key == SDL_SCANCODE_ESCAPE)
        return -1;
    keyb_p1[button] = key;
    return 0;
}
int Psyz_GetGamepadBinding_SDL3(unsigned button) {
    return button < 16 ? gamepad_p1[button] : -1;
}
int Psyz_SetGamepadBinding_SDL3(unsigned button, int binding) {
    if (button >= 16 || binding < 0 || binding > SDL_GAMEPAD_BUTTON_COUNT + 1)
        return -1;
    gamepad_p1[button] = binding;
    return 0;
}
static unsigned int PadRead_Gamepad(struct Gamepad* g) {
    if (!g || !g->dev)
        return 0;
#ifdef PE_PS5_PLUGINS
    extern int pe_ps5_mods_capture_pad(SDL_Gamepad *);
    if (pe_ps5_mods_capture_pad(g->dev)) return 0;
#endif
    unsigned int result = 0;
    for (unsigned i = 0; i < 16; ++i) {
        int binding = gamepad_p1[i];
        bool pressed =
            binding < SDL_GAMEPAD_BUTTON_COUNT
                ? SDL_GetGamepadButton(g->dev, (SDL_GamepadButton)binding)
                : SDL_GetGamepadAxis(
                      g->dev, binding == SDL_GAMEPAD_BUTTON_COUNT
                                  ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                  : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
        if (pressed)
            result |= 1u << i;
    }
    return result;
}

static unsigned int SinglePadRead(int id) {
    if (!is_pads_init) {
        WARNF("PadInit not called");
        return 0;
    }
    if (id < 0 || id >= LEN(gamepads)) {
        WARNF("invalid pad id %d", id);
        return 0;
    }

    u_long pressed = 0;
    if (id == 0) {
        if (SDL_HasKeyboard()) {
            pressed |= PadRead_Keyboard(keyb_p1, LEN(keyb_p1));
        }
#ifdef PLATFORM_IOS
        pressed |= Psyz_IosReadTouchControls();
#endif
    }
    return pressed | PadRead_Gamepad(&gamepads[id]);
}

static unsigned char AxisToByte(short v) {
    int u = ((int)v + 32768) >> 8;
    if (u < 0) {
        u = 0;
    }
    if (u > 0xFF) {
        u = 0xFF;
    }
    return (unsigned char)u;
}

static bool PortHasHostInput(int port) {
    if (port < 0 || port >= LEN(gamepads)) {
        return false;
    }
    if (gamepads[port].dev) {
        return true;
    }
    if (port == 0 && SDL_HasKeyboard()) {
        return true;
    }
#ifdef PLATFORM_IOS
    if (port == 0 && Psyz_IosTouchControlsActive()) {
        return true;
    }
#endif
    return false;
}

static void BuildPadFrame(int port, PsyzControllerKind kind, char* buf) {
    memset(buf, 0, PSYZ_PAD_BUF_LEN);
    // No physical input for this port -> report as disconnected regardless of
    // the requested kind. Games look at buf[0]=0xFF / buf[1]=0xFF to know.
    if (kind != PSYZ_CTRL_DISCONNECTED && !PortHasHostInput(port)) {
        kind = PSYZ_CTRL_DISCONNECTED;
    }
    buf[0] = 0x00;
    buf[1] = (char)kind;

    switch (kind) {
    case PSYZ_CTRL_DIGITAL_PAD:
    case PSYZ_CTRL_ANALOG_PAD:
    case PSYZ_CTRL_ANALOG_STICK: {
        unsigned int pressed = SinglePadRead(port);
        if (kind == PSYZ_CTRL_ANALOG_STICK) {
            // The analog stick (SCPH-1110) has no L3/R3; on real hardware
            // those bits always read as released.
            pressed &= ~(PADi | PADj);
        }
        buf[2] = (char)(~(pressed >> 8) & 0xFF);
        buf[3] = (char)(~pressed & 0xFF);

        if (kind == PSYZ_CTRL_DIGITAL_PAD) {
            break;
        }
        unsigned char rx = 0x80, ry = 0x80, lx = 0x80, ly = 0x80;
        if (gamepads[port].dev) {
            SDL_Gamepad* d = gamepads[port].dev;
#ifdef PE_PS5_PLUGINS
            extern int pe_ps5_mods_capture_pad(SDL_Gamepad *);
            if (!pe_ps5_mods_capture_pad(d)) {
#endif
            rx = AxisToByte(SDL_GetGamepadAxis(d, SDL_GAMEPAD_AXIS_RIGHTX));
            ry = AxisToByte(SDL_GetGamepadAxis(d, SDL_GAMEPAD_AXIS_RIGHTY));
            lx = AxisToByte(SDL_GetGamepadAxis(d, SDL_GAMEPAD_AXIS_LEFTX));
            ly = AxisToByte(SDL_GetGamepadAxis(d, SDL_GAMEPAD_AXIS_LEFTY));
#ifdef PE_PS5_PLUGINS
            }
#endif
        }
        buf[4] = (char)rx;
        buf[5] = (char)ry;
        buf[6] = (char)lx;
        buf[7] = (char)ly;
        break;
    }
    case PSYZ_CTRL_MOUSE:
        // TODO: read SDL mouse state into buf[3] buttons and buf[4..5] motion
        buf[2] = (char)0xFF;
        buf[3] = (char)0xFC;
        buf[4] = 0x00;
        buf[5] = 0x00;
        break;
    case PSYZ_CTRL_KEYBOARD:
        LOG_ONCE("keyboard controller kind not implemented");
        break;
    case PSYZ_CTRL_DISCONNECTED:
        memset(buf, 0xFF, PSYZ_PAD_BUF_LEN);
        break;
    default:
        WARNF("unknown controller kind 0x%02X", (unsigned)kind);
        break;
    }
}

void Psyz_PadsPoll(void) {
    char frame[PSYZ_PAD_BUF_LEN];

    PollEvents();
    for (int port = 0; port < 2; port++) {
        BuildPadFrame(
            port, Psyz_PadsSetKind(port, 0, PSYZ_CTRL_QUERY_KIND), frame);
        Psyz_PadsSet(port, frame, sizeof(frame));
    }
}

#ifndef PLATFORM_IOS
/* The window display's fullscreen mode at the field rate (within 1%: 50 Hz
 * for PAL's 49.75, 59.94 or 60 Hz for NTSC's 59.82) at the desktop's size
 * and density, or NULL when the desktop already runs at it or the display
 * has none. */
static const SDL_DisplayMode* FieldRateDisplayMode(void) {
    static SDL_DisplayMode chosen;
    const SDL_DisplayID display = WindowDisplay();
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    if (!desktop ||
        SDL_fabs(desktop->refresh_rate - target_frame_rate) <
            target_frame_rate * 0.01) {
        return NULL;
    }
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    const SDL_DisplayMode* best = NULL;
    for (int i = 0; modes && i < count; ++i) {
        const SDL_DisplayMode* m = modes[i];
        const double off = SDL_fabs(m->refresh_rate - target_frame_rate);
        if (m->w != desktop->w || m->h != desktop->h ||
            m->pixel_density != desktop->pixel_density ||
            off >= target_frame_rate * 0.01)
            continue;
        if (!best || off < SDL_fabs(best->refresh_rate - target_frame_rate))
            best = m;
    }
    if (best)
        chosen = *best;
    SDL_free(modes);
    return best ? &chosen : NULL;
}

/* Desktop fullscreen keeps the user display mode; with
 * fullscreen_field_rate it switches to one at the field rate if there is. */
static void ApplyFullscreenMode(void) {
    if (!sdl3_window || !is_fullscreen)
        return;
    const SDL_DisplayMode* mode =
        fullscreen_field_rate ? FieldRateDisplayMode() : NULL;
    if (!SDL_SetWindowFullscreenMode(sdl3_window, mode)) {
        WARNF("failed to select the fullscreen mode: %s", SDL_GetError());
        if (!mode || !SDL_SetWindowFullscreenMode(sdl3_window, NULL))
            return;
        mode = NULL;
    }
    if (mode)
        INFOF("fullscreen at %dx%d %.3f Hz", mode->w, mode->h,
              mode->refresh_rate);
    SDL_SyncWindow(sdl3_window);
    SDL_SetAtomicInt(&timing_reset_requested, 1); /* the new refresh rate */
}

static void SetFullScreen(bool isFullscreen) {
    if (!sdl3_window) {
        return;
    }
    if (is_fullscreen == isFullscreen) {
        return;
    }
    is_fullscreen = isFullscreen;
    if (is_fullscreen) {
        const SDL_DisplayMode* mode =
            fullscreen_field_rate ? FieldRateDisplayMode() : NULL;
        if (!SDL_SetWindowFullscreenMode(sdl3_window, mode) &&
            (!mode || !SDL_SetWindowFullscreenMode(sdl3_window, NULL))) {
            WARNF("failed to select desktop fullscreen: %s", SDL_GetError());
            is_fullscreen = false;
            return;
        }
        if (mode)
            INFOF("fullscreen at %dx%d %.3f Hz", mode->w, mode->h,
                  mode->refresh_rate);
        if (!SDL_SetWindowFullscreen(sdl3_window, true)) {
            WARNF("failed to enter fullscreen: %s", SDL_GetError());
            is_fullscreen = false;
            return;
        }
        INFOF("entered fullscreen");
    } else {
        SDL_SetWindowFullscreen(sdl3_window, false);
        INFOF("returned to windowed mode");
    }
    SDL_SetAtomicInt(&timing_reset_requested, 1); /* the refresh rate */
    SDL_SyncWindow(sdl3_window);
    SDL_GetWindowSizeInPixels(
        sdl3_window, &wnd_size_in_pixels.w, &wnd_size_in_pixels.h);
}
#endif

SDL_Window* Psyz_GetWindow_SDL3(void) { return sdl3_window; }

void Psyz_VideoSetFullscreenFieldRate(int enabled) {
    if (fullscreen_field_rate == (enabled != 0))
        return;
    fullscreen_field_rate = enabled != 0;
#ifndef PLATFORM_IOS
    ApplyFullscreenMode();
#endif
}

int Psyz_VideoGetFullscreenFieldRate(void) { return fullscreen_field_rate; }

int Psyz_VideoFullscreenFieldRateAvailable(void) {
#ifndef PLATFORM_IOS
    if (!sdl3_window)
        return 0;
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(WindowDisplay());
    return FieldRateDisplayMode() != NULL ||
           (desktop && SDL_fabs(desktop->refresh_rate - target_frame_rate) <
                           target_frame_rate * 0.01);
#else
    return 0;
#endif
}

bool Psyz_SetFullscreen_SDL3(bool enabled) {
#ifndef PLATFORM_IOS
    if (!sdl3_window)
        return !enabled;
    /* Also account for window-manager changes, not just our F4 shortcut. */
    is_fullscreen =
        (SDL_GetWindowFlags(sdl3_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SetFullScreen(enabled);
    return is_fullscreen == enabled;
#else
    (void)enabled;
    return false;
#endif
}

static void PollEvents(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (overlay_event_filter && overlay_event_filter(&event))
            continue;
        if (overlay_event_cb) {
            overlay_event_cb(&event);
        }
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            AddGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            RemoveGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_QUIT:
            SDL_SetAtomicInt(&quit_requested, 1);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            SDL_GetWindowSizeInPixels(
                sdl3_window, &wnd_size_in_pixels.w, &wnd_size_in_pixels.h);
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
            /* another refresh rate: driver VSync or the limiter */
            SDL_SetAtomicInt(&timing_reset_requested, 1);
            break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
#ifdef PLATFORM_IOS
            SDL_GetWindowSizeInPixels(
                sdl3_window, &wnd_size_in_pixels.w, &wnd_size_in_pixels.h);
#else
            SetWindowSizeInPixels(wnd_size_in_pixels.w, wnd_size_in_pixels.h);
#endif
            break;
#ifndef PLATFORM_IOS
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                SDL_SetAtomicInt(&quit_requested, 1);
            }
            if (event.key.scancode == SDL_SCANCODE_F6) {
                debug_show_vram ^= 1;
            }
            if (event.key.scancode == SDL_SCANCODE_F4 && !event.key.repeat) {
                SetFullScreen(!is_fullscreen);
            }
            break;
#endif
        default:
            break;
        }
    }
#ifndef PLATFORM_IOS
    if (Psyz_QuitRequested()
#ifdef PE_PS5_GPU_NATIVE
        && !native_quit_in_progress
#endif
    ) {
#ifdef PE_PS5_GPU_NATIVE
        native_quit_in_progress=true;
        SDL_Log("[Native quit] stage=event-request");
#endif
        QuitPlatform();
        exit(0);
    }
#endif
}

#endif // SDL3_COMMON_H
