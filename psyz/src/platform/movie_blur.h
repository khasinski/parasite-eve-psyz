// Project original optional native RGB24 backdrop. No frame submission/waits.
#include "movie_blur_shaders.h"
#include "movie_lifecycle.h"
static int gameplay_fill=1; /* Legacy native policy until host supplies persisted preference. */
void Psyz_VideoSetGameplayFill(int enabled){enabled=enabled!=0;if(gameplay_fill!=enabled){gameplay_fill=enabled;INFOF("Gameplay final fill requested=%d scope=RGB15 camera-policy=unchanged",enabled);}}
static int movie_presentation=-1; /* -1 retains legacy API semantics until host selects. */
static int movie_blur_requested,movie_blur_failed,movie_blur_status=-1;
static SDL_GPUTexture *movie_blur_input,*movie_blur_temp,*movie_blur_output;
static SDL_GPUGraphicsPipeline *movie_blur_pipeline;
static SDL_GPUSampler *movie_blur_sampler;
static Uint32 movie_blur_w,movie_blur_h;
static unsigned movie_blur_log_count,movie_blur_source_serial;
/* Bounded source/output decisions, not a claim of observed pixels. */
static unsigned movie_output_trace_count;
static int movie_output_trace_valid;
static unsigned movie_output_trace_last[20];
static void MovieBlurOutputPolicyTrace(SDL_GPUBlitRegion src,WndSize win,SDL_Rect prior,SDL_Rect actual,bool rgb24){
    unsigned values[20]={(unsigned)rgb24,(unsigned)title_present_active,(unsigned)movie_blur_requested,
        src.x,src.y,src.w,src.h,(unsigned)win.w,(unsigned)win.h,(unsigned)prior.x,(unsigned)prior.y,(unsigned)prior.w,(unsigned)prior.h,
        (unsigned)actual.x,(unsigned)actual.y,(unsigned)actual.w,(unsigned)actual.h,(unsigned)debug_show_vram,(unsigned)widescreen,(unsigned)native_movie_active};
    if(movie_output_trace_count>=64||(movie_output_trace_valid&&!memcmp(values,movie_output_trace_last,sizeof(values))))return;
    memcpy(movie_output_trace_last,values,sizeof(values));movie_output_trace_valid=1;movie_output_trace_count++;
    const char *why=debug_show_vram?"debug-vram":!native_movie_active?(title_present_active&&rgb24?"enhanced-still":gameplay_fill?"gameplay-final-stretch-fill":"gameplay-selected-aspect"):
        movie_presentation==0?"movie-stretch16:9":movie_presentation==2?"movie-original-bars":!movie_blur_requested?"movie-backdrop-disabled":
        (actual.x==0&&actual.y==0&&actual.w==win.w&&actual.h==win.h)?"movie-no-unused-space":"movie-aspect-center-blur-requested";
    INFOF("OUTPUT policy=%s rgb24=%d title=%d blur_requested=%d source=%u,%u/%ux%u physical=%dx%d before=%d,%d/%dx%d final=%d,%d/%dx%d wide_source=%d actual_movie=%d observed_pixels=0",
        why,rgb24,title_present_active,movie_blur_requested,src.x,src.y,src.w,src.h,win.w,win.h,prior.x,prior.y,prior.w,prior.h,actual.x,actual.y,actual.w,actual.h,widescreen,native_movie_active);
}
static const char *movie_blur_reason="uninitialized";

void Psyz_VideoSetMoviePresentation(int mode){if(mode<0||mode>2)mode=0;if(mode!=movie_presentation){movie_presentation=mode;movie_blur_status=-1;INFOF("Movie presentation mode=%d 0Stretch16:9-1Aspect+blur-2Originalbars",mode);}}
void Psyz_VideoSetMovieBlur(int enabled){enabled=enabled!=0;if(enabled!=movie_blur_requested){movie_blur_requested=enabled;movie_blur_failed=0;movie_blur_status=-1;INFOF("Movie backdrop requested=%d original-center-aspect=preserved",enabled);}}
static void MovieBlurLog(int status){if(status!=movie_blur_status&&movie_blur_log_count<32){INFOF("Movie backdrop requested=%d effective=%s reason=%s stage=original-movie-lifecycle source_serial=%u input=%ux%u rendered_output=unobserved",movie_blur_requested,status==1?"blur-commands-recorded":status==2?"original-fallback":"original-bars-or-no-bars",movie_blur_reason,native_movie_source_serial,movie_blur_w,movie_blur_h);movie_blur_log_count++;movie_blur_status=status;}}
static void MovieBlurDestroy(void){
    if(movie_blur_input)SDL_ReleaseGPUTexture(device,movie_blur_input);
    if(movie_blur_temp)SDL_ReleaseGPUTexture(device,movie_blur_temp);
    if(movie_blur_output)SDL_ReleaseGPUTexture(device,movie_blur_output);
    if(movie_blur_pipeline)SDL_ReleaseGPUGraphicsPipeline(device,movie_blur_pipeline);
    if(movie_blur_sampler)SDL_ReleaseGPUSampler(device,movie_blur_sampler);
    movie_blur_input=movie_blur_temp=movie_blur_output=NULL;movie_blur_pipeline=NULL;movie_blur_sampler=NULL;movie_blur_w=movie_blur_h=0;
}
static bool MovieBlurPrepare(Uint32 w,Uint32 h){
    if(!w||!h||w>256||h>256)return false;
    if(!movie_blur_pipeline){
        SDL_GPUShaderCreateInfo si={0};si.code=cpe_fullscreen_vert;si.code_size=sizeof(cpe_fullscreen_vert);si.entrypoint="main";si.format=SDL_GPU_SHADERFORMAT_SPIRV;si.stage=SDL_GPU_SHADERSTAGE_VERTEX;
        SDL_GPUShader *vs=SDL_CreateGPUShader(device,&si);si.code=cpe_movie_blur_frag;si.code_size=sizeof(cpe_movie_blur_frag);si.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;si.num_samplers=1;si.num_uniform_buffers=1;
        SDL_GPUShader *fs=SDL_CreateGPUShader(device,&si);
        if(vs&&fs){SDL_GPUColorTargetDescription ct={0};ct.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;SDL_GPUGraphicsPipelineCreateInfo pi={0};pi.vertex_shader=vs;pi.fragment_shader=fs;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pi.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;pi.target_info.color_target_descriptions=&ct;pi.target_info.num_color_targets=1;movie_blur_pipeline=SDL_CreateGPUGraphicsPipeline(device,&pi);}
        if(vs)SDL_ReleaseGPUShader(device,vs);if(fs)SDL_ReleaseGPUShader(device,fs);
        if(!movie_blur_pipeline)return false;
    }
    if(!movie_blur_sampler){SDL_GPUSamplerCreateInfo si={0};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;movie_blur_sampler=SDL_CreateGPUSampler(device,&si);if(!movie_blur_sampler)return false;}
    if(movie_blur_w==w&&movie_blur_h==h&&movie_blur_input&&movie_blur_temp&&movie_blur_output)return true;
    SDL_GPUTextureCreateInfo ti={0};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=w;ti.height=h;ti.layer_count_or_depth=1;ti.num_levels=1;ti.sample_count=SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *a=SDL_CreateGPUTexture(device,&ti),*b=SDL_CreateGPUTexture(device,&ti),*c=SDL_CreateGPUTexture(device,&ti);
    if(!a||!b||!c){if(a)SDL_ReleaseGPUTexture(device,a);if(b)SDL_ReleaseGPUTexture(device,b);if(c)SDL_ReleaseGPUTexture(device,c);return false;}
    if(movie_blur_input)SDL_ReleaseGPUTexture(device,movie_blur_input);if(movie_blur_temp)SDL_ReleaseGPUTexture(device,movie_blur_temp);if(movie_blur_output)SDL_ReleaseGPUTexture(device,movie_blur_output);
    movie_blur_input=a;movie_blur_temp=b;movie_blur_output=c;movie_blur_w=w;movie_blur_h=h;return true;
}
static bool MovieBlurPass(SDL_GPUCommandBuffer *cmd,SDL_GPUTexture *src,SDL_GPUTexture *dst,float x,float y,float dim){
    SDL_GPUColorTargetInfo target={0};target.texture=dst;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;target.cycle=true;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(cmd,&target,1,NULL);if(!pass)return false;
    SDL_BindGPUGraphicsPipeline(pass,movie_blur_pipeline);SDL_GPUTextureSamplerBinding binding={src,movie_blur_sampler};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);float u[4]={x,y,dim,0};SDL_PushGPUFragmentUniformData(cmd,0,u,sizeof(u));SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);return true;
}
/* Observe only after normal movie/unused-space admission. A failure stays
 * latched for this movie, but does not permanently disable later movies. */
static int MovieBlurTakeSourceRetry(void){
    if(!native_movie_source_serial||movie_blur_source_serial==native_movie_source_serial)return 0;
    movie_blur_source_serial=native_movie_source_serial;int retry=movie_blur_failed;
    movie_blur_failed=0;movie_blur_status=-1;return retry;
}
static bool MovieBlurPresent(SDL_GPUCommandBuffer *cmd,SDL_GPUBlitRegion src,SDL_GPUTexture *swapchain,WndSize win,SDL_Rect center,bool rgb24){
    if(movie_presentation>=0&&movie_presentation!=1){movie_blur_reason=movie_presentation==0?"movie-stretch-selected":"original-bars-selected";MovieBlurLog(0);return false;}
    movie_blur_reason=!movie_blur_requested?"disabled":!native_movie_active?"not-original-movie":!src.texture||!src.w||!src.h?"invalid-source":win.w<=0||win.h<=0?"invalid-output":
        center.x==0&&center.y==0&&center.w==win.w&&center.h==win.h?"no-unused-space":"recording";
    if(!movie_blur_requested||!native_movie_active||!src.texture||!src.w||!src.h||win.w<=0||win.h<=0||(center.x==0&&center.y==0&&center.w==win.w&&center.h==win.h)){MovieBlurLog(0);return false;}
    if(MovieBlurTakeSourceRetry()&&movie_blur_log_count<32)INFOF("Movie backdrop new-source retry source_serial=%u previous-resource-failure=1 no-per-frame-retry=1",native_movie_source_serial);
    if(movie_blur_failed){movie_blur_reason="latched-resource-failure";MovieBlurLog(2);return false;}
    float ratio=SDL_min(1.f,256.f/(float)SDL_max(src.w,src.h));Uint32 w=SDL_max(1u,(Uint32)((float)src.w*ratio)),h=SDL_max(1u,(Uint32)((float)src.h*ratio));
    if(!MovieBlurPrepare(w,h))goto failed;
    SDL_GPUBlitInfo compact={0};compact.source=src;compact.destination.texture=movie_blur_input;compact.destination.w=w;compact.destination.h=h;compact.filter=SDL_GPU_FILTER_LINEAR;compact.load_op=SDL_GPU_LOADOP_DONT_CARE;compact.cycle=true;SDL_BlitGPUTexture(cmd,&compact);
    if(!MovieBlurPass(cmd,movie_blur_input,movie_blur_temp,1.f/(float)w,0,1)||!MovieBlurPass(cmd,movie_blur_temp,movie_blur_output,0,1.f/(float)h,.65f))goto failed;
    SDL_GPUBlitInfo background={0};background.source.texture=movie_blur_output;background.source.w=w;background.source.h=h;
    if((uint64_t)w*(Uint32)win.h>(uint64_t)h*(Uint32)win.w){background.source.w=SDL_max(1u,(Uint32)((uint64_t)h*(Uint32)win.w/(Uint32)win.h));background.source.x=(w-background.source.w)/2;}
    else {background.source.h=SDL_max(1u,(Uint32)((uint64_t)w*(Uint32)win.h/(Uint32)win.w));background.source.y=(h-background.source.h)/2;}
    background.destination.texture=swapchain;background.destination.w=(Uint32)win.w;background.destination.h=(Uint32)win.h;background.load_op=SDL_GPU_LOADOP_CLEAR;background.clear_color=(SDL_FColor){0,0,0,1};background.filter=SDL_GPU_FILTER_LINEAR;SDL_BlitGPUTexture(cmd,&background);movie_blur_reason="background-blit-recorded-center-must-LOAD";MovieBlurLog(1);return true;
failed:movie_blur_failed=1;movie_blur_reason="resource-or-pass-failure";WARNF("Movie backdrop fallback: %s",SDL_GetError());MovieBlurLog(2);return false;
}
