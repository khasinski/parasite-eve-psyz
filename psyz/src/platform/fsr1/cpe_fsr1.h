#ifndef CPE_FSR1_H
#define CPE_FSR1_H
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
// Isolated integration prototype, not linked into the playable game.
// Call only on the renderer thread, outside any active copy/render pass.
// Default is disabled. Original presentation remains the caller's fallback.
typedef struct CpeFsr1 CpeFsr1;
CpeFsr1 *CpeFsr1_Create(SDL_GPUDevice *device);
void CpeFsr1_SetEnabled(CpeFsr1 *state, bool enabled);
// Input must be a dedicated, compact, single-sample SDR UNORM texture with
// SAMPLER usage. Do NOT pass the VRAM atlas or a cropped subregion: copy the
// exact already-composed game rectangle to its own texture first. UI is drawn
// AFTER the returned image is composed into the aspect-preserving destination.
// Input RGB must already be display-referred (no sRGB sampler conversion).
// Both input dimensions <= output dimensions. No downscale or frame generation.
// sharpness_stops: 0 strongest RCAS, 2 weak. Errors never submit/cancel cmd.
// NULL means no FSR output: caller must choose original presentation or cancel
// its unsubmitted frame; no raw/unfiltered image is labelled FSR by this API.
SDL_GPUTexture *CpeFsr1_Record(CpeFsr1 *state, SDL_GPUCommandBuffer *cmd,
    SDL_GPUTexture *input, Uint32 input_w, Uint32 input_h,
    Uint32 output_w, Uint32 output_h, float sharpness_stops);
// Caller retains device until destroyed, after outstanding commands retire.
void CpeFsr1_Destroy(CpeFsr1 *state);
#endif
