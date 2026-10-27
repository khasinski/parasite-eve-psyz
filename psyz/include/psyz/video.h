#ifndef PSYZ_VIDEO_H
#define PSYZ_VIDEO_H

/**
 * @file video.h
 * @brief Window, display timing and frame presentation endpoints.
 */

#include <psyz/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the title of the game window
 *
 * Can also be set with psyz_title(<target> <str>) on CMakeLists
 *
 * @param str Window title string, truncated past 255 characters
 */
void Psyz_SetTitle(const char* str);

typedef enum {
    /**
     * Auto-detect use of driver VSync (default)
     * - Tests monitor refresh rate on initialization (adds ~30ms startup time)
     * - Uses driver VSync if monitor is detected between 57Hz-63Hz
     *   (±5% tolerance). This covers both 59.94Hz (NTSC) and 60Hz
     * - Uses manual limiter otherwise (144Hz, 165Hz, etc.)
     * - Balances performance and accuracy automatically
     */
    PSYZ_VSYNC_AUTO,

    /**
     * Always use driver VSync
     * Pros: Zero CPU overhead and no frame pacing on matching refresh rates
     * Cons: Game will run faster on high refresh monitors
     * Cons: Framerate always matches monitor exactly.
     *   On 60Hz monitor: runs at 60fps instead of NTSC 59.94fps
     *   causing ~1 second drift every ~16.7 minutes of gameplay
     * Real NTSC PSX hardware runs at 59.94Hz (60/1.001)
     *   PAL PSX hardware runs at exactly 50Hz
     * Best when combined with driver control panel frame rate limit
     * IMPORTANT: PAL games (50fps) will run at 60fps on 60Hz monitors without
     *   driver-level limiting, which is incorrect
     */
    PSYZ_VSYNC_ON,

    /**
     * Always use internal manual frame limiter
     * Pros: Precise 59.94fps (NTSC) / 50fps (PAL) matching real hardware
     * Pros: Safe for VRR displays
     * Pros: Consistent timing across all monitor refresh rates
     * Cons: ~6% CPU usage on one core (1ms busy-wait per frame for precision)
     * Cons: May have minor frame pacing variance on non-VRR displays
     */
    PSYZ_VSYNC_OFF,

    /**
     * Disable both driver VSync and the internal frame limiter.
     * Most games will be unplayable with this option, but it's useful for:
     *   - Adapting game on variable framerate using a deltaTime.
     *   - Reproduce gameplay with simulatd input on CI at high speed.
     *   - Measure frame time end-to-end and if game is CPU or GPU bound.
     */
    PSYZ_VSYNC_LIMITLESS,
} PsyzVsyncMode;

typedef enum {
    PSYZ_DITHER_AUTO, /**< let the game decide whether to dither (default) */
    PSYZ_DITHER_OFF,  /**< disable dithering and RGB555 shading quantization on desktop GPUs */
} PsyzDitherMode;

typedef enum {
    PSYZ_ASPECT_DISPLAY, /**< aspect from PS1 H/V display ranges (default) */
    PSYZ_ASPECT_SQUARE,  /**< 1:1 from framebuffer (pixel-perfect) */
} PsyzAspectMode;

typedef struct {
    double last_frame_time_us;       /**< duration of last frame */
    double last_draw_time_us;        /**< render time excluding vsync wait */
    double target_frame_time_us;     /**< target frame time */
    unsigned long long total_frames; /**< total frames rendered */
    int using_driver_vsync;          /**< 1 for VSync, 0 for limiter */
} PsyzVideoStats;

/**
 * @brief Get the current VSync mode
 *
 * @return current VSync mode
 */
PsyzVsyncMode Psyz_VideoGetVsyncMode(void);

/**
 * @brief Set VSync mode (default: AUTO)
 *
 * @param mode VSync mode to set
 * @return 0 on success, -1 if invalid mode
 */
int Psyz_VideoSetVsyncMode(PsyzVsyncMode mode);

/**
 * @brief Get the current dithering mode
 *
 * @return current dithering mode
 */
PsyzDitherMode Psyz_VideoGetDitheringMode(void);

/**
 * @brief Set dithering mode (default: AUTO)
 *
 * @param mode Dithering mode to set
 * @return 0 on success, -1 if invalid mode
 */
int Psyz_VideoSetDitheringMode(PsyzDitherMode mode);

/**
 * @brief Get the current aspect mode
 *
 * @return current aspect mode
 */
PsyzAspectMode Psyz_VideoGetAspectMode(void);

/**
 * @brief Select how the presented aspect ratio is determined
 *
 * DISPLAY: use the game horizontal/vertical display (display sync) ranges,
 *   so different game resolutions present at the intended physical proportions.
 *   This is the accurate and default behaviour, originally intended to control
 *   the beam range of a CRT display. This is not pixel-perfect.
 * SQUARE: present framebuffer pixels 1:1, ignoring sync ranges. This presents
 *   the game pixel-perfect to the display at the cost of intent-accuracy.
 *
 * @param mode Aspect mode to set
 * @return 0 on success, -1 if invalid mode
 */
int Psyz_VideoSetAspectMode(PsyzAspectMode mode);

/**
 * @brief Get the resolution a game should target to render pixel-perfect
 *
 * Fixed-display targets (PSP, and future NDS/Saturn) return a physical size
 * the ported game can adopt to fully use the screen:
 * - PSYZ_ASPECT_SQUARE: the full physical resolution (PSP: 480x272), 1:1.
 * - PSYZ_ASPECT_DISPLAY: the largest 4:3 area at full height (PSP: 362x272);
 *   adopting it keeps output pixel-perfect while preserving the intended
 *   aspect ratio.
 * PC targets return the current window size in pixels.
 *
 * @return display size in pixels
 */
PsyzSize Psyz_VideoGetDisplaySize(void);

/**
 * @brief Choose where on the screen the presented output lands
 *
 * Only meaningful on fixed-display targets (PSP); ignored on PC.
 * The default rect {0,0,0,0} centers the output according to the aspect
 * mode. A non-empty rect places the output's top-left at (x, y) and scales
 * it to fit w x h, overriding the aspect mode's own sizing. The result is
 * still clipped to the physical screen, so a rect that extends past the
 * edge is cropped.
 *
 * @param rect target area on the display, or {0,0,0,0} to center
 */
void Psyz_VideoSetDrawArea(PsyzRect rect);

/** Maximum accepted internal resolution multiplier */
#define PSYZ_INTERNAL_RES_MAX 8

#ifndef __PSP__
/**
 * Targets without this capability must:
 *   - Implement Psyz_VideoSetInternalResolution as no-op
 *   - Always return 1 for Psyz_VideoGetInternalResolution
 */
#define PSYZ_HAS_INTERNAL_RESOLUTION_SCALE
#endif

/**
 * @brief Set the internal rendering resolution multiplier (default: 1)
 *
 * Internally multiplies horizontal and vertical resolution, giving 3D
 * geometry more sub-pixel precision (smoother edges, less shimmer).
 * Safe to call at runtime; the change applies on the next presented frame
 * without needing to restart the game.
 *
 * @param multiplier Integer scale factor, between 1 to PSYZ_INTERNAL_RES_MAX
 * @return 0 on success, -1 if multiplier < 1
 */
int Psyz_VideoSetInternalResolution(unsigned multiplier);

/**
 * @brief Get the current internal resolution multiplier
 *
 * @return current multiplier (>= 1)
 */
unsigned Psyz_VideoGetInternalResolution(void);

/**
 * @brief Enable or disable subpixel 3D geometry & perspective UV correction
 *
 * Preserves native 1x VRAM and GTE integer state for game logic while using
 * subpixel vertex coordinates and perspective-correct attribute interpolation
 * on the GPU display target.
 *
 * @param enabled Non-zero to enable, 0 to disable
 * @return 0 on success
 */
int Psyz_VideoSetGeometryCorrection(int enabled);

/**
 * @brief Query whether subpixel 3D geometry correction is enabled
 *
 * @return Non-zero when enabled, 0 when disabled
 */
int Psyz_VideoGetGeometryCorrection(void);

/** Display-only 4x SSAA (twice each axis). Native VRAM remains unchanged. */
int Psyz_VideoSetAntialiasing(int enabled);
int Psyz_VideoGetAntialiasing(void);
/** Palette-aware bilinear filtering for tagged 3D models only. */
int Psyz_VideoSetModelFiltering(int enabled);
int Psyz_VideoGetModelFiltering(void);

/**
 * @brief Widescreen: present a 4:3 game at 16:9 with more of its picture
 *
 * While on, everything drawn into the display buffers (drawing areas inside
 * the display's VRAM columns and the first two display heights of VRAM rows,
 * where double buffers are stacked) is squeezed to 3/4 of its width around
 * the buffer's centre. What the game draws from -1/6 to 7/6 of its screen
 * width then fills the buffer, and the buffer is presented 4/3 wider, so
 * everything keeps its shape. Untextured fills that cover the whole screen
 * width are widened by the margin on both sides. The game has to draw the
 * extra picture itself (wider culling: a sixth of the screen width past each
 * side, plus the view's shift, see Psyz_VideoSetWidescreenPicture).
 * 24-bit (movie) display stays 4:3. A drawing area clip narrower than the
 * screen is squeezed with what it clips. Native VRAM is squeezed too, so
 * read-backs of the display buffers see the presented picture.
 * Only the SDL GPU renderer supports it; elsewhere it stays off.
 *
 * @param enabled Non-zero to enable, 0 to disable
 * @return 0 on success, -1 when not supported
 */
int Psyz_VideoSetWidescreen(int enabled);
int Psyz_VideoGetWidescreen(void);

/**
 * @brief Tell the presenter where the game's picture is in one buffer
 *
 * left and right are game pixels relative to the screen's left edge. The
 * 16:9 view is slid towards the side that has picture until its edge meets
 * the picture's end, at most as far as keeps the game's own screen inside it
 * (39 pixels at 320x224); a span narrower than the view shows only the 4:3
 * screen, unshifted, with even bars. The
 * buffer's squeeze follows the shift, so the view always fits in the buffer:
 * the game should draw its picture up to 0.3 of the screen width past each
 * side. Everything outside the span stays black. Call it before drawing the
 * frame into that buffer. left >= right presents the whole buffer, unshifted.
 *
 * @param buffer_y VRAM row of the buffer's drawing area
 */
void Psyz_VideoSetWidescreenPicture(int buffer_y, int left, int right);

/**
 * @brief Draw centred in the 16:9 view, ignoring the view's shift
 *
 * While on, drawing into a display buffer lands where it would with the view
 * unshifted: the game screen's centre is the view's centre. For layers that
 * belong to the screen rather than the room (menus over a room whose view is
 * shifted at its end). Psyz_VideoSetWidescreenPicture turns it off. To switch
 * it in ordering-table order, call it from a GP0 command handler (see
 * Psyz_GpuRegisterCommandHandler).
 *
 * @param enabled Non-zero to draw centred, 0 to follow the view's shift
 */
void Psyz_VideoSetWidescreenCentred(int enabled);

/**
 * @brief Psyz_VideoSetWidescreenPicture for a room that belongs between pixels
 *
 * As Psyz_VideoSetWidescreenPicture, for a frame whose room (its background
 * and models) belongs dx, dy game pixels right and down of where the game
 * draws it, for a camera the game keeps in whole pixels. Widescreen only:
 * the room is drawn moved by whole pixels of the shown picture, chosen so
 * the squeezed room keeps its texels' shape, and the presenter shows the
 * rest, so slow pans move evenly at any internal resolution. A view that
 * follows the picture's end follows it to the fraction. Only layers drawn
 * while Psyz_VideoSetWidescreenField is on move; the presenter's part moves
 * the whole picture, up to half a pixel of the shown picture. The game must
 * draw 3 game pixels more past each side. Native VRAM under a high
 * resolution picture keeps the game's own picture.
 *
 * @param buffer_y VRAM row of the buffer's drawing area
 * @param dx, dy Where the room belongs, -2 to 2 game pixels
 * @param camera_x The camera's whole-pixel x the room is drawn for (screen
 *                 x = world x - camera_x plus a constant)
 */
void Psyz_VideoSetWidescreenPictureShifted(int buffer_y, int left, int right, float dx, float dy,
                                           int camera_x);

/**
 * @brief Mark drawing as the room (see Psyz_VideoSetWidescreenPictureShifted)
 *
 * Psyz_VideoSetWidescreenPicture* turns it off. To switch it in
 * ordering-table order, call it from a GP0 command handler (see
 * Psyz_GpuRegisterCommandHandler).
 *
 * @param enabled Non-zero while the room is drawn
 */
void Psyz_VideoSetWidescreenField(int enabled);

/**
 * @brief Synchronize with vertical blank
 *
 * Synchronize with the refresh rate mode set in Psyz_VideoSetVsyncMode.
 * The interface is very similar to libetc VSync.
 *
 * @param mode Synchronization mode:
 *             - 0: Present and wait for the next vertical blank
 *             - 1: Return immediately (non-blocking)
 *             - Negative: Return immediately (non-blocking)
 * @return Simulates the SDK VSync return values as closely as possible.
 */
int Psyz_VideoVSync(int mode);

/**
 * @brief Run the game faster than real time (default: 1)
 *
 * Frame pacing, the vertical blank count, root counters and audio all run
 * `speed` times faster, so the game behaves as at normal speed. The VSync
 * mode and the swapchain are left as they are: with driver VSync only the
 * newest frame of each refresh is shown, so presents never queue up.
 * Audio plays at a higher pitch.
 *
 * @param speed 1 (normal) to PSYZ_SPEED_MAX
 * @return 0 on success, -1 if invalid speed
 */
#define PSYZ_SPEED_MAX 4
int Psyz_VideoSetSpeed(unsigned speed);
unsigned Psyz_VideoGetSpeed(void);

/**
 * @brief Get frame timing statistics
 *
 * @param stats Output structure to fill
 * @return 0 on success, -1 if stats is NULL or platform not initialized
 */
int Psyz_VideoStats(PsyzVideoStats* stats);

/**
 * @brief Get frame output as a byte array
 *
 * This function is very slow.
 *
 * @param w Output frame width
 * @param h Output frame height
 * @return NULL on failure, or RGB888 buffer to be destroyed with free(ptr)
 */
unsigned char* Psyz_VideoAllocCapturedFrame(int* w, int* h);

/**
 * @brief Capture the rendered display before window scaling as RGB888.
 *
 * Includes internal-resolution detail for RGB555 GPU output. RGB24 movies
 * and platforms without internal scaling retain their native dimensions.
 * Does not change the dimensions or behavior of AllocCapturedFrame.
 *
 * @param w Required output width (zero on failure)
 * @param h Required output height (zero on failure)
 * @return NULL on failure, or a buffer to release with free(ptr)
 */
unsigned char* Psyz_VideoAllocRenderedFrame(int* w, int* h);

/**
 * @brief Dump the whole video memory plane as RGB888
 *
 * @param w Output VRAM width
 * @param h Output VRAM height
 * @return NULL on failure, or RGB888 buffer to be destroyed with free(ptr)
 */
unsigned char* Psyz_VideoAllocVramDump(int* w, int* h);

#ifdef __cplusplus
}
#endif

#endif
