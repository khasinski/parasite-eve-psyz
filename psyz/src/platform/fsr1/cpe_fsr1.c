// CPE original SDL_GPU integration prototype. AMD FSR1 MIT notices retained.
#include "cpe_fsr1.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define A_CPU 1
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "third_party/fsr1/ffx_a.h"
#include "third_party/fsr1/ffx_fsr1.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "generated/fsr1_shaders.h"

struct CpeFsr1 {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *easu, *rcas;
    SDL_GPUSampler *sampler;
    SDL_GPUTexture *intermediate, *output;
    Uint32 width,height;
    bool enabled;
};
static SDL_GPUShader *shader(SDL_GPUDevice *d,const unsigned char *code,
    size_t size,SDL_GPUShaderStage stage) {
    SDL_GPUShaderCreateInfo i={0};
    i.code=code;i.code_size=size;i.entrypoint="main";
    i.format=SDL_GPU_SHADERFORMAT_SPIRV;i.stage=stage;
    if(stage==SDL_GPU_SHADERSTAGE_FRAGMENT){i.num_samplers=1;i.num_uniform_buffers=1;}
    return SDL_CreateGPUShader(d,&i);
}
static SDL_GPUGraphicsPipeline *pipeline(CpeFsr1 *s,SDL_GPUShader *vs,SDL_GPUShader *fs) {
    SDL_GPUColorTargetDescription color={0};
    color.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo i={0};
    i.vertex_shader=vs;i.fragment_shader=fs;
    i.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    i.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
    i.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
    i.target_info.color_target_descriptions=&color;i.target_info.num_color_targets=1;
    return SDL_CreateGPUGraphicsPipeline(s->device,&i);
}
CpeFsr1 *CpeFsr1_Create(SDL_GPUDevice *d) {
    if(!d){SDL_SetError("FSR1 requires GPU device");return NULL;}
    CpeFsr1 *s=SDL_calloc(1,sizeof(*s));if(!s)return NULL;s->device=d;
    SDL_GPUShader *vs=shader(d,cpe_fullscreen_vert,sizeof(cpe_fullscreen_vert),SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *e=shader(d,cpe_fsr1_easu_frag,sizeof(cpe_fsr1_easu_frag),SDL_GPU_SHADERSTAGE_FRAGMENT);
    SDL_GPUShader *r=shader(d,cpe_fsr1_rcas_frag,sizeof(cpe_fsr1_rcas_frag),SDL_GPU_SHADERSTAGE_FRAGMENT);
    if(vs&&e&&r){s->easu=pipeline(s,vs,e);s->rcas=pipeline(s,vs,r);}
    if(vs)SDL_ReleaseGPUShader(d,vs);if(e)SDL_ReleaseGPUShader(d,e);if(r)SDL_ReleaseGPUShader(d,r);
    SDL_GPUSamplerCreateInfo si={0};
    si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    s->sampler=SDL_CreateGPUSampler(d,&si);
    if(!s->easu||!s->rcas||!s->sampler){CpeFsr1_Destroy(s);return NULL;}
    return s; // disabled by zero initialization
}
void CpeFsr1_SetEnabled(CpeFsr1 *s,bool enabled){if(s)s->enabled=enabled;}
static bool targets(CpeFsr1 *s,Uint32 w,Uint32 h) {
    if(s->width==w&&s->height==h&&s->intermediate&&s->output)return true;
    // Two RGBA16F textures, at most 256 MiB in total, arithmetic widened first.
    if(!w||!h||w>8192||h>8192||(uint64_t)w*h*16>256u*1024u*1024u)
        return SDL_SetError("FSR1 output exceeds isolated budget");
    SDL_GPUTextureCreateInfo t={0};t.type=SDL_GPU_TEXTURETYPE_2D;
    t.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    t.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    t.width=w;t.height=h;t.layer_count_or_depth=1;t.num_levels=1;t.sample_count=SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *a=SDL_CreateGPUTexture(s->device,&t),*b=SDL_CreateGPUTexture(s->device,&t);
    if(!a||!b){if(a)SDL_ReleaseGPUTexture(s->device,a);if(b)SDL_ReleaseGPUTexture(s->device,b);return false;}
    // SDL release is deferred for referenced command resources; no raw frees.
    if(s->intermediate)SDL_ReleaseGPUTexture(s->device,s->intermediate);
    if(s->output)SDL_ReleaseGPUTexture(s->device,s->output);
    s->intermediate=a;s->output=b;s->width=w;s->height=h;return true;
}
static bool pass(CpeFsr1 *s,SDL_GPUCommandBuffer *cmd,SDL_GPUTexture *src,
    SDL_GPUTexture *dst,SDL_GPUGraphicsPipeline *p,const void *u,Uint32 bytes) {
    SDL_GPUColorTargetInfo t={0};t.texture=dst;t.load_op=SDL_GPU_LOADOP_DONT_CARE;
    t.store_op=SDL_GPU_STOREOP_STORE;t.cycle=true;
    SDL_GPURenderPass *rp=SDL_BeginGPURenderPass(cmd,&t,1,NULL);if(!rp)return false;
    SDL_BindGPUGraphicsPipeline(rp,p);
    SDL_GPUTextureSamplerBinding bind={src,s->sampler};
    SDL_BindGPUFragmentSamplers(rp,0,&bind,1);
    SDL_PushGPUFragmentUniformData(cmd,0,u,bytes);
    SDL_DrawGPUPrimitives(rp,3,1,0,0);SDL_EndGPURenderPass(rp);return true;
}
SDL_GPUTexture *CpeFsr1_Record(CpeFsr1 *s,SDL_GPUCommandBuffer *cmd,
    SDL_GPUTexture *in,Uint32 iw,Uint32 ih,Uint32 ow,Uint32 oh,float sharp) {
    if(!s||!s->enabled)return NULL;
    if(!cmd||!in||!iw||!ih||iw>ow||ih>oh||!isfinite(sharp)||sharp<0||sharp>2){
        SDL_SetError("FSR1 invalid viewport or RCAS sharpness");return NULL;
    }
    if(in==s->intermediate||in==s->output){SDL_SetError("FSR1 input aliases output");return NULL;}
    if(!targets(s,ow,oh))return NULL;
    AU1 c0[4],c1[4],c2[4],c3[4],rcas[4];Uint32 easu[16];
    FsrEasuCon(c0,c1,c2,c3,(float)iw,(float)ih,(float)iw,(float)ih,(float)ow,(float)oh);
    memcpy(easu,c0,16);memcpy(easu+4,c1,16);memcpy(easu+8,c2,16);memcpy(easu+12,c3,16);
    FsrRcasCon(rcas,sharp);
    if(!pass(s,cmd,in,s->intermediate,s->easu,easu,sizeof(easu)))return NULL;
    if(!pass(s,cmd,s->intermediate,s->output,s->rcas,rcas,sizeof(rcas)))return NULL;
    return s->output;
}
void CpeFsr1_Destroy(CpeFsr1 *s){
    if(!s)return;
    if(s->intermediate)SDL_ReleaseGPUTexture(s->device,s->intermediate);
    if(s->output)SDL_ReleaseGPUTexture(s->device,s->output);
    if(s->sampler)SDL_ReleaseGPUSampler(s->device,s->sampler);
    if(s->easu)SDL_ReleaseGPUGraphicsPipeline(s->device,s->easu);
    if(s->rcas)SDL_ReleaseGPUGraphicsPipeline(s->device,s->rcas);
    SDL_free(s);
}
