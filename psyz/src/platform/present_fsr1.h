// Native-only optional presentation adapter. Included after PsyZ device state.
#include "fsr1/cpe_fsr1.h"
static CpeFsr1 *fsr1_state;
static SDL_GPUTexture *fsr1_input;
static Uint32 fsr1_iw,fsr1_ih;
static int fsr1_requested,fsr1_sharpness=1,fsr1_failed,fsr1_effective;
static unsigned fsr1_log_count;
static char fsr1_last_status[192];
void Psyz_VideoSetFsr1(int enabled,int sharpness) {
    enabled=enabled!=0;sharpness=SDL_clamp(sharpness,0,2);
    if(enabled!=fsr1_requested||sharpness!=fsr1_sharpness) {
        fsr1_failed=0;fsr1_last_status[0]=0;
        INFOF("FSR1 setting requested=%d sharpness_stops=%d (0 strongest,2 soft)",enabled,sharpness);
    }
    fsr1_requested=enabled;fsr1_sharpness=sharpness;
    if(!enabled)fsr1_effective=0;
}
int Psyz_VideoGetFsr1Status(void){return fsr1_effective;}
static void Fsr1Status(const char *status,SDL_GPUBlitRegion src,SDL_Rect dst) {
    fsr1_effective=!fsr1_requested?0:!SDL_strcmp(status,"EASU+RCAS-recorded")?2:fsr1_failed?3:1;
    char message[192];SDL_snprintf(message,sizeof(message),"requested=%d effective=%s input=%ux%u output=%dx%d sharpness_stops=%d stage=%s bars=aspect-policy",fsr1_requested,status,src.w,src.h,dst.w,dst.h,fsr1_sharpness,display_rgb24?"RGB24-movie-or-still":"game-image");
    if(SDL_strcmp(message,fsr1_last_status)&&fsr1_log_count<32){
        INFOF("FSR1 %s rendered_output=unobserved",message);fsr1_log_count++;
        SDL_strlcpy(fsr1_last_status,message,sizeof(fsr1_last_status));
    }
}
static SDL_GPUBlitRegion Fsr1Present(SDL_GPUCommandBuffer *cmd,SDL_GPUBlitRegion src,SDL_Rect dst) {
    if(!fsr1_requested){Fsr1Status("off",src,dst);return src;}
    if(dst.w<=0||dst.h<=0||!src.w||!src.h){Fsr1Status("original-invalid-extent",src,dst);return src;}
    if(src.w>(Uint32)dst.w||src.h>(Uint32)dst.h){Fsr1Status("original-downscale",src,dst);return src;}
    if(src.w>8192||src.h>8192||dst.w>8192||dst.h>8192||
       (uint64_t)src.w*src.h*4+(uint64_t)dst.w*dst.h*16>160u*1024u*1024u){
        Fsr1Status("original-budget",src,dst);return src;
    }
    if(fsr1_failed){Fsr1Status("original-error-latched",src,dst);return src;}
    if(!fsr1_state){
        fsr1_state=CpeFsr1_Create(device);
        if(!fsr1_state){WARNF("FSR1 optional pipeline unavailable: %s",SDL_GetError());fsr1_failed=1;Fsr1Status("original-pipeline-failed",src,dst);return src;}
        CpeFsr1_SetEnabled(fsr1_state,true);
    }
    if(!fsr1_input||fsr1_iw!=src.w||fsr1_ih!=src.h){
        SDL_GPUTextureCreateInfo t={0};t.type=SDL_GPU_TEXTURETYPE_2D;
        t.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        t.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        t.width=src.w;t.height=src.h;t.layer_count_or_depth=1;t.num_levels=1;t.sample_count=SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *replacement=SDL_CreateGPUTexture(device,&t);
        if(!replacement){WARNF("FSR1 compact image allocation failed: %s",SDL_GetError());fsr1_failed=1;Fsr1Status("original-input-allocation-failed",src,dst);return src;}
        if(fsr1_input)SDL_ReleaseGPUTexture(device,fsr1_input);
        fsr1_input=replacement;fsr1_iw=src.w;fsr1_ih=src.h;
    }
    // Crop BEFORE gathering. A clamp sampler on the full VRAM texture would
    // still let EASU tap other pages outside the selected game rectangle.
    SDL_GPUBlitInfo crop={0};crop.source=src;
    crop.destination=(SDL_GPUBlitRegion){.texture=fsr1_input,.w=src.w,.h=src.h};
    crop.load_op=SDL_GPU_LOADOP_DONT_CARE;crop.filter=SDL_GPU_FILTER_NEAREST;crop.cycle=true;
    SDL_BlitGPUTexture(cmd,&crop);
    SDL_GPUTexture *filtered=CpeFsr1_Record(fsr1_state,cmd,fsr1_input,src.w,src.h,(Uint32)dst.w,(Uint32)dst.h,(float)fsr1_sharpness);
    if(!filtered){WARNF("FSR1 optional pass failed: %s",SDL_GetError());fsr1_failed=1;Fsr1Status("original-filter-failed",src,dst);return src;}
    Fsr1Status("EASU+RCAS-recorded",src,dst);
    return (SDL_GPUBlitRegion){.texture=filtered,.w=(Uint32)dst.w,.h=(Uint32)dst.h};
}
static void Fsr1Destroy(void) {
    // Caller only reaches this after the existing GPU retirement succeeds.
    if(fsr1_input)SDL_ReleaseGPUTexture(device,fsr1_input);
    fsr1_input=NULL;fsr1_iw=fsr1_ih=0;
    CpeFsr1_Destroy(fsr1_state);fsr1_state=NULL;fsr1_failed=0;
}
