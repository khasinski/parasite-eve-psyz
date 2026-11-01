/* Project-original optional GPU dialogue alpha sheet. Existing SDL_GPU owner. */
static bool SubmitCmdAndWait(void);

int Psyz_GpuDialogueStateIdle(void){for(unsigned i=0;i<2;i++)if(dialogue_watches[i].overflow||native_fonts[i].quarantine_texture||native_fonts[i].quarantine_transfer)return 0;return dialogue_next_glyph<0&&dialogue_font_batch<0&&!n_vertices&&!n_indices;}
void Psyz_GpuSetFontContextEnabled(unsigned context,int on){if(context>=2)return;on=on!=0;if(native_fonts[context].enabled==on)return;if(device&&!UseSoftwareBackend())Draw_FlushBuffer();native_fonts[context].enabled=on;dialogue_next_glyph=-1;}
static void DialogueFontDestroy(void){
    DialogueFontWatchReset();
    for(unsigned context=0;context<2;context++){native_fonts[context].valid=0;
    if(native_fonts[context].texture)SDL_ReleaseGPUTexture(device,native_fonts[context].texture);
    if(native_fonts[context].sampler)SDL_ReleaseGPUSampler(device,native_fonts[context].sampler);
    if(native_fonts[context].quarantine_texture)SDL_ReleaseGPUTexture(device,native_fonts[context].quarantine_texture);
    if(native_fonts[context].quarantine_transfer)SDL_ReleaseGPUTransferBuffer(device,native_fonts[context].quarantine_transfer);
    native_fonts[context].texture=native_fonts[context].quarantine_texture=NULL;native_fonts[context].sampler=NULL;native_fonts[context].quarantine_transfer=NULL;dialogue_next_glyph=-1;dialogue_font_batch=-1;}
}
int Psyz_GpuHasFontContext(unsigned context){return context<2&&native_fonts[context].valid&&device&&!UseSoftwareBackend()&&scaled_vram_render&&internal_res>1&&native_fonts[context].texture&&native_fonts[context].sampler&&!native_fonts[context].quarantine_texture;}
int Psyz_GpuLoadFontContext(unsigned context,const unsigned char *rgba,unsigned bytes){
    if(context>=2||!rgba||bytes!=416u*520u*4u||!device||UseSoftwareBackend()||native_fonts[context].quarantine_texture||!RetireGpu())return 0;
    SDL_GPUTextureCreateInfo ti={.type=SDL_GPU_TEXTURETYPE_2D,.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER,.width=416,.height=520,.layer_count_or_depth=1,.num_levels=1};
    SDL_GPUTexture *texture=SDL_CreateGPUTexture(device,&ti);
    SDL_GPUTransferBufferCreateInfo bi={.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,.size=bytes};
    SDL_GPUTransferBuffer *transfer=texture?SDL_CreateGPUTransferBuffer(device,&bi):NULL;
    if(!texture||!transfer){if(texture)SDL_ReleaseGPUTexture(device,texture);return 0;}
    void *data=SDL_MapGPUTransferBuffer(device,transfer,false);
    if(!data){SDL_ReleaseGPUTransferBuffer(device,transfer);SDL_ReleaseGPUTexture(device,texture);return 0;}
    memcpy(data,rgba,bytes);SDL_UnmapGPUTransferBuffer(device,transfer);
    SDL_GPUCommandBuffer *cmd=AcquireCmd();SDL_GPUCopyPass *copy=cmd?SDL_BeginGPUCopyPass(cmd):NULL;
    if(!copy){SDL_ReleaseGPUTransferBuffer(device,transfer);SDL_ReleaseGPUTexture(device,texture);return 0;}
    SDL_GPUTextureTransferInfo src={.transfer_buffer=transfer,.pixels_per_row=416,.rows_per_layer=520};
    SDL_GPUTextureRegion dst={.texture=texture,.w=416,.h=520,.d=1};
    SDL_UploadToGPUTexture(copy,&src,&dst,false);SDL_EndGPUCopyPass(copy);
    if(!SubmitCmdAndWait()){
        /* An uncertain submission still references these resources. Preserve
         * them until whole-device retirement; never release into possible use. */
        if(!gpu_retirement_uncertain){SDL_ReleaseGPUTransferBuffer(device,transfer);SDL_ReleaseGPUTexture(device,texture);}
        else{native_fonts[context].quarantine_texture=texture;native_fonts[context].quarantine_transfer=transfer;}return 0;
    }
    SDL_ReleaseGPUTransferBuffer(device,transfer);
    if(!native_fonts[context].sampler){SDL_GPUSamplerCreateInfo si={.min_filter=SDL_GPU_FILTER_LINEAR,.mag_filter=SDL_GPU_FILTER_LINEAR,.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,.address_mode_u=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,.address_mode_v=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};native_fonts[context].sampler=SDL_CreateGPUSampler(device,&si);}
    if(!native_fonts[context].sampler){SDL_ReleaseGPUTexture(device,texture);return 0;}
    if(native_fonts[context].texture)SDL_ReleaseGPUTexture(device,native_fonts[context].texture);native_fonts[context].texture=texture;native_fonts[context].valid=1;return 1;
}
void Psyz_GpuSelectFontGlyph(unsigned context,unsigned code,unsigned crop,unsigned width){dialogue_next_context=context<2?(int)context:-1;dialogue_next_crop=crop;dialogue_next_width=width;dialogue_next_glyph=context<2&&native_fonts[context].valid&&native_fonts[context].enabled&&code<78&&crop<12&&width&&width<=12-crop?(int)code:-1;}
void Psyz_GpuSelectDialogueGlyph(unsigned code){Psyz_GpuSelectFontGlyph(0,code,0,12);}
void Psyz_GpuSetDialogueFontEnabled(int on){Psyz_GpuSetFontContextEnabled(0,on);}
int Psyz_GpuHasDialogueFont(void){return Psyz_GpuHasFontContext(0);}
int Psyz_GpuLoadDialogueFont(const unsigned char *rgba,unsigned bytes){return Psyz_GpuLoadFontContext(0,rgba,bytes);}
