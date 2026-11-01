/* SPDX-License-Identifier: MIT
 * Project-original native SDL_GPU module. No copied third-party filter code. */
#include "movie_cleanup.h"
#include "movie_cleanup_shaders.h"
#include <SDL3/SDL_log.h>
#include <stdlib.h>
#include <string.h>
struct CpeMovieCleanup {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler *sampler;
    SDL_GPUTexture *output;
    Uint32 width,height;
    int failed,last_mode,last_status;
    unsigned logs;
};
static void status(CpeMovieCleanup *s,int mode,int value,const char *reason){
    if(s->logs<24&&(s->last_mode!=mode||s->last_status!=value)){
        SDL_Log("[Movie cleanup] mode=%d status=%s output=%ux%u source-logical-spatial-only=1 observed_pixels=0",mode,reason,s->width,s->height);
        s->logs++;
    }
    s->last_mode=mode;s->last_status=value;
}
CpeMovieCleanup *CpeMovieCleanup_Create(SDL_GPUDevice *device){
    if(!device)return NULL;
    CpeMovieCleanup *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->device=device;s->last_mode=s->last_status=-1;return s;
}
void CpeMovieCleanup_Destroy(CpeMovieCleanup *s){
    if(!s)return;
    if(s->output)SDL_ReleaseGPUTexture(s->device,s->output);
    if(s->pipeline)SDL_ReleaseGPUGraphicsPipeline(s->device,s->pipeline);
    if(s->sampler)SDL_ReleaseGPUSampler(s->device,s->sampler);
    free(s);
}
void CpeMovieCleanup_ResetFailure(CpeMovieCleanup *s){if(s){s->failed=0;s->last_mode=s->last_status=-1;}}
static bool prepare(CpeMovieCleanup *s,Uint32 w,Uint32 h){
    if(!s->pipeline){
        SDL_GPUShaderCreateInfo si={0};si.code=cpe_movie_cleanup_vert;si.code_size=sizeof(cpe_movie_cleanup_vert);si.entrypoint="main";si.format=SDL_GPU_SHADERFORMAT_SPIRV;si.stage=SDL_GPU_SHADERSTAGE_VERTEX;
        SDL_GPUShader *vs=SDL_CreateGPUShader(s->device,&si);
        si.code=cpe_movie_cleanup_frag;si.code_size=sizeof(cpe_movie_cleanup_frag);si.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;si.num_samplers=1;si.num_uniform_buffers=1;
        SDL_GPUShader *fs=SDL_CreateGPUShader(s->device,&si);
        if(vs&&fs){SDL_GPUColorTargetDescription ct={0};ct.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;SDL_GPUGraphicsPipelineCreateInfo pi={0};pi.vertex_shader=vs;pi.fragment_shader=fs;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pi.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;pi.target_info.color_target_descriptions=&ct;pi.target_info.num_color_targets=1;s->pipeline=SDL_CreateGPUGraphicsPipeline(s->device,&pi);}
        if(vs)SDL_ReleaseGPUShader(s->device,vs);if(fs)SDL_ReleaseGPUShader(s->device,fs);
        if(!s->pipeline)return false;
    }
    if(!s->sampler){SDL_GPUSamplerCreateInfo si={0};si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;s->sampler=SDL_CreateGPUSampler(s->device,&si);if(!s->sampler)return false;}
    if(s->output&&s->width==w&&s->height==h)return true;
    SDL_GPUTextureCreateInfo ti={0};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;ti.width=w;ti.height=h;ti.layer_count_or_depth=1;ti.num_levels=1;ti.sample_count=SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *next=SDL_CreateGPUTexture(s->device,&ti);if(!next)return false;
    if(s->output)SDL_ReleaseGPUTexture(s->device,s->output);s->output=next;s->width=w;s->height=h;return true;
}
SDL_GPUBlitRegion CpeMovieCleanup_Apply(CpeMovieCleanup *s,SDL_GPUCommandBuffer *cmd,SDL_GPUBlitRegion src,bool video,Uint32 w,Uint32 h,int mode){
    if(!s)return src;
    if(mode<0||mode>3)mode=0;
    if(mode!=s->last_mode)s->failed=0;
    if(!mode||!video){status(s,mode,0,!mode?"original-OFF":"original-not-video");return src;}
    if(!cmd||!src.texture||src.texture==s->output||!src.w||!src.h||!w||!h||w>1024||h>512||src.x>131072||src.y>131072||src.w>131072||src.h>131072||src.mip_level||src.layer_or_depth_plane){status(s,mode,2,"original-invalid-bounds");return src;}
    if(s->failed){status(s,mode,2,"original-latched-resource-failure");return src;}
    if(!prepare(s,w,h))goto fail;
    SDL_GPUColorTargetInfo target={0};target.texture=s->output;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;target.cycle=true;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(cmd,&target,1,NULL);if(!pass)goto fail;
    SDL_BindGPUGraphicsPipeline(pass,s->pipeline);SDL_GPUViewport viewport={0,0,(float)w,(float)h,0,1};SDL_Rect scissor={0,0,(int)w,(int)h};SDL_SetGPUViewport(pass,&viewport);SDL_SetGPUScissor(pass,&scissor);
    SDL_GPUTextureSamplerBinding binding={src.texture,s->sampler};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
    float parameters[8]={(float)src.x,(float)src.y,(float)src.w,(float)src.h,(float)w,(float)h,mode==3?0.12f:mode==1?0.06f:0.10f,mode==3?0.70f:mode==1?0.30f:0.55f};
    SDL_PushGPUFragmentUniformData(cmd,0,parameters,sizeof(parameters));SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    status(s,mode,1,"spatial-commands-recorded");return (SDL_GPUBlitRegion){.texture=s->output,.w=w,.h=h};
fail:s->failed=1;SDL_Log("[Movie cleanup] resource fallback: %s",SDL_GetError());status(s,mode,2,"original-resource-fallback");return src;
}
