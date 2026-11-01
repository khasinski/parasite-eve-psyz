#ifndef PSYZ_OVERLAY_SDL3_GPU_H
#define PSYZ_OVERLAY_SDL3_GPU_H
#include <SDL3/SDL.h>
#include <psyz/overlay_sdl3.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Terminal GPU state: borrowed error string valid until process exit. No
 * retry/reinitialization or CPU fallback after failure. Main loop also receives
 * SDL_EVENT_QUIT; callers can distinguish it from an ordinary user Quit. */
int Psyz_GpuFailed(void);
const char *Psyz_GpuFirstError(void);
int Psyz_GpuRetirementUncertain(void);
/* One bounded diagnostic on the next prepared viewport; no rendering policy change. */
void Psyz_GpuTraceAspectOnce(const char *phase);
/* Optional native FSR1, default off. Strength stops0(strong)..2(soft).
 * Status0 off,1 bypassed,2 EASU+RCAS recorded,3 original fallback. */
void Psyz_VideoSetFsr1(int enabled,int sharpness);
int Psyz_VideoGetFsr1Status(void);
void Psyz_VideoSetMovieBlur(int enabled);

// Called once after SDL3/SDL_GPU initialization completes.
// Use this to initialize overlay backends (e.g., ImGui_ImplSDLGPU3_Init).
// Query swapchain texture format with SDL_GetGPUSwapchainTextureFormat.
typedef void (*PsyzOverlayInitCB_SDL3GPU)(
    SDL_Window* window, SDL_GPUDevice* device);

// Called each presented frame inside a render pass targeting the swapchain
// texture, after the PS1 framebuffer has been blitted to it.
// Use with ImGui_ImplSDLGPU3_RenderDrawData.
typedef void (*PsyzOverlayRenderCB_SDL3GPU)(
    SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass);

// Register the SDL3+SDL_GPU initialization callback.
// Returns the previous callback, or NULL if none was set.
PsyzOverlayInitCB_SDL3GPU Psyz_OverlayInit_SDL3GPU(
    PsyzOverlayInitCB_SDL3GPU cb);

// Register the SDL3+SDL_GPU render callback.
// Returns the previous callback, or NULL if none was set.
PsyzOverlayRenderCB_SDL3GPU Psyz_OverlayRender_SDL3GPU(
    PsyzOverlayRenderCB_SDL3GPU cb);

#ifdef __cplusplus
}
#endif

#endif // PSYZ_OVERLAY_SDL3_GPU_H
