/* SPDX-License-Identifier: MIT
 * Native-only integration seam, included after movie_blur.h/device state. */
#include "movie_cleanup.h"
static CpeMovieCleanup *movie_cleanup_state;
static unsigned movie_cleanup_source_serial;
static int movie_cleanup_create_failed;
static int movie_cleanup_requested; /* Original OFF until explicit selection. */
void Psyz_VideoSetMovieCleanup(int mode){
    if(mode<0||mode>3)mode=0;
    if(mode!=movie_cleanup_requested){
        CpeMovieCleanup_ResetFailure(movie_cleanup_state);movie_cleanup_create_failed=0;
        SDL_Log("[Movie cleanup] requested=%d 0Original-1Gentle-2Stronger-3Block-smoothing actual-video-only=1",mode);
    }
    movie_cleanup_requested=mode;
}
static SDL_GPUBlitRegion MovieCleanupPresent(SDL_GPUCommandBuffer *cmd,SDL_GPUBlitRegion src){
    if(!movie_cleanup_requested||!native_movie_active)return src;
    if(native_movie_source_serial&&movie_cleanup_source_serial!=native_movie_source_serial){
        movie_cleanup_source_serial=native_movie_source_serial;
        CpeMovieCleanup_ResetFailure(movie_cleanup_state);movie_cleanup_create_failed=0;
    }
    if(!movie_cleanup_state&&!movie_cleanup_create_failed){
        movie_cleanup_state=CpeMovieCleanup_Create(device);
        if(!movie_cleanup_state){movie_cleanup_create_failed=1;SDL_Log("[Movie cleanup] original-state-allocation-fallback source_serial=%u retry=next-movie-or-option-change",native_movie_source_serial);}
    }
    return CpeMovieCleanup_Apply(movie_cleanup_state,cmd,src,true,(Uint32)display_size.x,(Uint32)display_size.y,movie_cleanup_requested);
}
static void MovieCleanupDestroy(void){CpeMovieCleanup_Destroy(movie_cleanup_state);movie_cleanup_state=NULL;movie_cleanup_source_serial=0;movie_cleanup_create_failed=0;}
