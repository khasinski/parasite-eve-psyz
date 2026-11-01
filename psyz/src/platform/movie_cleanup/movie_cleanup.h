/* SPDX-License-Identifier: MIT
 * Project-original spatial movie cleanup; no learned model or temporal state. */
#ifndef CPE_MOVIE_CLEANUP_H
#define CPE_MOVIE_CLEANUP_H
#include <SDL3/SDL_gpu.h>
#include <stdbool.h>
typedef struct CpeMovieCleanup CpeMovieCleanup;
enum { CPE_MOVIE_ORIGINAL=0, CPE_MOVIE_GENTLE=1, CPE_MOVIE_STRONGER=2, CPE_MOVIE_BLOCKS=3 };
CpeMovieCleanup *CpeMovieCleanup_Create(SDL_GPUDevice *device);
void CpeMovieCleanup_Destroy(CpeMovieCleanup *state);
/* Explicit user mode change permits one fresh resource attempt. */
void CpeMovieCleanup_ResetFailure(CpeMovieCleanup *state);
/* actual_video must come from the original movie lifecycle, not RGB24/still
 * heuristics. logical_w/h are authored decoded-video dimensions. A scaled
 * RGB15 source region is sampled at logical pixel centers, never atlas edges.
 * OFF/not-video/failure returns the original source rectangle unchanged. */
SDL_GPUBlitRegion CpeMovieCleanup_Apply(CpeMovieCleanup *state,
    SDL_GPUCommandBuffer *cmd,SDL_GPUBlitRegion source,bool actual_video,
    Uint32 logical_w,Uint32 logical_h,int mode);
#endif
