/* CPU native-pack material adapter experiment.
 * Original implementation for this integration; no emulator implementation.
 * Included by sdl3_gpu.c after LoadHdImage so existing WebP/PNG decoding is reused.
 * Ordered presentation consumer; canonical VRAM remains authoritative.
 */
#ifndef PE_CPU_MATERIAL_ADAPTER_H
#define PE_CPU_MATERIAL_ADAPTER_H
#define CPU_MATERIAL_SLOTS 32
#define CPU_MATERIAL_FAILED_SLOTS 256
#define CPU_MATERIAL_BUDGET (16u*1024u*1024u)
typedef struct {
    uint64_t key;
    SDL_Surface *art, *reference;
    unsigned columns, rows;
    size_t bytes;
} CpuMaterialAtlas;
typedef struct {
    const CpuMaterialAtlas *atlas;
    unsigned cell;
    bool extension;
} CpuMaterialSelection;
static CpuMaterialAtlas cpu_material_cache[CPU_MATERIAL_SLOTS];
static unsigned cpu_material_count;
/* Current configured root/reset epoch only. Fixed FIFO; failed entries own no
 * surfaces and never evict successful atlases or disturb held selections.
 * All failures, including allocator/decoder OOM, retry after OFF->ON, explicit
 * atlas reset, root change, or FIFO eviction. No file polling/mtime promise. */
static uint64_t cpu_material_failed_keys[CPU_MATERIAL_FAILED_SLOTS];
static unsigned cpu_material_failed_count,cpu_material_failed_next;
static size_t cpu_material_bytes;
static CpuMaterialSelection cpu_material_next;

static int cpu_material_requested;
static char cpu_material_directory[4096];
static uint64_t cm_hits,cm_misses,cm_loaded,cm_failed,cm_unsupported,cm_failed_hits;
static uint64_t cm_selected,cm_select_miss,cm_samples,cm_rejected,cm_present,cm_fallback;
static unsigned cm_logs;
static Uint64 cm_last_log;
static void CmCount(uint64_t *n) { if(*n!=UINT64_MAX)++*n; }
static int CpuMaterialRememberFailure(uint64_t key) {
    cpu_material_failed_keys[cpu_material_failed_next]=key;
    if(cpu_material_failed_count<CPU_MATERIAL_FAILED_SLOTS)++cpu_material_failed_count;
    if(++cpu_material_failed_next==CPU_MATERIAL_FAILED_SLOTS)cpu_material_failed_next=0;
    CmCount(&cm_failed);return -1;
}
int Psyz_CpuFieldMaterialAvailable(void) {
#ifdef PE_PS5
    return ps5_interactive_software && sdl3_window && is_platform_init_successful;
#else
    return 0;
#endif
}
static bool CpuMaterialEnabled(void) {
#ifdef PE_PS5
    return cpu_material_requested && *cpu_material_directory && Psyz_CpuFieldMaterialAvailable();
#else
    return false;
#endif
}
static void CpuMaterialSummary(int force) {
    Uint64 now=SDL_GetTicks();
    if((!force && (now-cm_last_log<10000)))return;
    cm_last_log=now;++cm_logs;
    unsigned long long covered=0,total=0;
    SoftwareRasterizer_MaterialCoverage(&covered,&total);
    INFOF("CPU fieldHD requested=%d available=%d eligible=%d cache=%u bytes=%zu failed_keys=%u failed_hit=%llu hit=%llu miss=%llu loaded=%llu failed=%llu unsupported=%llu selected=%llu select_miss=%llu sampled=%llu rejected=%llu ordered_HD_writes=%llu/textured_sprite_samples=%llu prepared_HD=%llu fallback=%llu (ordered accepted HD samples/textured sprite samples this epoch; cache counters cumulative)",
        cpu_material_requested,Psyz_CpuFieldMaterialAvailable(),CpuMaterialEnabled(),cpu_material_count,cpu_material_bytes,
        cpu_material_failed_count,(unsigned long long)cm_failed_hits,
        (unsigned long long)cm_hits,(unsigned long long)cm_misses,(unsigned long long)cm_loaded,(unsigned long long)cm_failed,
        (unsigned long long)cm_unsupported,(unsigned long long)cm_selected,(unsigned long long)cm_select_miss,
        (unsigned long long)cm_samples,(unsigned long long)cm_rejected,covered,total,
        (unsigned long long)cm_present,(unsigned long long)cm_fallback);
}
static void CpuMaterialReset(void) {
    CpuMaterialSummary(1);
    SoftwareRasterizer_Reject2x();
    SoftwareRasterizer_InvalidateMaterial();
    SoftwareRasterizer_SetMaterialSampler(NULL);
    cpu_material_next=(CpuMaterialSelection){0};
    for(unsigned i=0;i<cpu_material_count;++i) {
        SDL_DestroySurface(cpu_material_cache[i].art);
        SDL_DestroySurface(cpu_material_cache[i].reference);
    }
    memset(cpu_material_cache,0,sizeof(cpu_material_cache));
    cpu_material_count=0;cpu_material_bytes=0;
    memset(cpu_material_failed_keys,0,sizeof(cpu_material_failed_keys));
    cpu_material_failed_count=0;cpu_material_failed_next=0;
}
int Psyz_CpuFieldMaterialConfigure(int enabled,const char *directory) {
    if(!directory)directory="";
    size_t n=strlen(directory);
    int valid=n<sizeof(cpu_material_directory) && (!n || (directory[0]=='/' &&
        !strstr(directory,"/../") && !strstr(directory,"/./") && !strchr(directory,'\\') &&
        !(n>=3 && !strcmp(directory+n-3,"/..")) && !(n>=2 && !strcmp(directory+n-2,"/."))));
    if(!valid){enabled=0;directory="";}
    enabled=enabled!=0;
    if(cpu_material_requested!=enabled || strcmp(cpu_material_directory,directory)) {
        CpuMaterialReset();
        cpu_material_requested=enabled;SDL_strlcpy(cpu_material_directory,directory,sizeof(cpu_material_directory));
        CpuMaterialSummary(1);
    }
    return CpuMaterialEnabled(); /* Actual current capability, never asset-success. */
}
const char *Psyz_CpuFieldMaterialDirectory(void) { return CpuMaterialEnabled()?cpu_material_directory:""; }
static bool CpuMaterialShape(SDL_Surface *s) {
    return s && s->w>0 && s->h>0 && s->w<=16*68 && s->h<=8192 &&
        s->w%68==0 && s->h%68==0 &&
        (uint64_t)s->w*s->h*8<=CPU_MATERIAL_BUDGET;
}
static int CpuMaterialLoad(uint64_t key,const char *filename) {
    if(!CpuMaterialEnabled() || !key || !filename) return 0;
    /* Only official separate-reference WebP layout in this bounded adapter.
     * Combined PNG/BMP and extension cells explicitly remain unsupported. */
    const char *ext=SDL_strrchr(filename,'.');
    if(!ext || SDL_strcasecmp(ext,".webp")){CmCount(&cm_unsupported);return 0;}
    /* The producer emits root/key.webp. Validate the actual pathname before
     * consulting either key cache: a stale producer root must not poison the
     * new configuration or falsely hit an atlas from it. Reset clears the
     * entire epoch, so generation counters cannot wrap into stale entries. */
    char expected[4096];
    int length=SDL_snprintf(expected,sizeof(expected),"%s/%016llx.webp",
        cpu_material_directory,(unsigned long long)key);
    if(length<0 || (size_t)length>=sizeof(expected) || strcmp(filename,expected))return 0;
    for(unsigned i=0;i<cpu_material_count;++i)
        if(cpu_material_cache[i].key==key){CmCount(&cm_hits);return 1;}
    for(unsigned i=0;i<cpu_material_failed_count;++i)
        if(cpu_material_failed_keys[i]==key){CmCount(&cm_failed_hits);return -1;}
    CmCount(&cm_misses);
    if(cpu_material_count==CPU_MATERIAL_SLOTS)return CpuMaterialRememberFailure(key);
    char path[4096];size_t prefix=(size_t)(ext-filename);
    if(prefix+sizeof(".ref.png")>sizeof(path))return CpuMaterialRememberFailure(key);
    memcpy(path,filename,prefix);memcpy(path+prefix,".ref.png",sizeof(".ref.png"));
    /* Keep both streams stable through metadata validation and decode. Never
     * use LoadHdImage here: its title/GPU allocation cap is intentionally larger.
     * No eviction: selections carry atlas pointers until reset invalidates them. */
    SDL_Surface *art=NULL,*reference=NULL,*converted=NULL;
    SDL_IOStream *webp=NULL,*pngfile=NULL;
    void *data=NULL;
    webp=SDL_IOFromFile(filename,"rb");pngfile=SDL_IOFromFile(path,"rb");
    if(!webp || !pngfile)goto fail;
    Sint64 compressed=SDL_GetIOSize(webp),pngsize=SDL_GetIOSize(pngfile);
    if(compressed<=0 || compressed>32*1024*1024 || pngsize<33 ||
       pngsize>32*1024*1024)goto fail;
    /* Compressed input is separately capped; only decoded surface storage is
     * charged to CPU_MATERIAL_BUDGET, not libwebp's internal scratch memory. */
    data=SDL_malloc((size_t)compressed);
    if(!data || SDL_ReadIO(webp,data,(size_t)compressed)!=(size_t)compressed)goto fail;
    WebPBitstreamFeatures features;
    if(WebPGetFeatures(data,(size_t)compressed,&features)!=VP8_STATUS_OK ||
       features.has_animation || features.width<=0 || features.height<=0 ||
       features.width>16*68 || features.height>8192 ||
       features.width%68 || features.height%68)goto fail;
    unsigned char png[33];
    static const unsigned char signature[16]={
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82};
    if(SDL_ReadIO(pngfile,png,sizeof(png))!=sizeof(png) ||
       memcmp(png,signature,sizeof(signature)))goto fail;
    uint32_t pw=((uint32_t)png[16]<<24)|((uint32_t)png[17]<<16)|
        ((uint32_t)png[18]<<8)|png[19];
    uint32_t ph=((uint32_t)png[20]<<24)|((uint32_t)png[21]<<16)|
        ((uint32_t)png[22]<<8)|png[23];
    if(pw!=(unsigned)features.width || ph!=(unsigned)features.height ||
       png[26] || png[27] || png[28]>1)goto fail;
    /* Valid PNG sample layouts only. SDL's decoder may expand 16-bit RGBA
     * to eight bytes/pixel before RGBA32 conversion; reserve that peak too. */
    unsigned depth=png[24],color=png[25];
    if(!((color==0 && (depth==1||depth==2||depth==4||depth==8||depth==16)) ||
         (color==3 && (depth==1||depth==2||depth==4||depth==8)) ||
         ((color==2||color==4||color==6) && (depth==8||depth==16))))goto fail;
    uint64_t pixels=(uint64_t)pw*ph;
    if(cpu_material_bytes>CPU_MATERIAL_BUDGET ||
       pixels>(CPU_MATERIAL_BUDGET-cpu_material_bytes)/16u)goto fail;
    if(SDL_SeekIO(pngfile,0,SDL_IO_SEEK_SET)!=0)goto fail;
    art=SDL_CreateSurface(features.width,features.height,SDL_PIXELFORMAT_RGBA32);
    if(!art || art->pitch!=features.width*4 ||
       !WebPDecodeRGBAInto(data,(size_t)compressed,art->pixels,
           (size_t)art->pitch*art->h,art->pitch))goto fail;
    SDL_free(data);data=NULL;
    reference=SDL_LoadPNG_IO(pngfile,false);
    if(!CpuMaterialShape(reference) || reference->w!=art->w || reference->h!=art->h)goto fail;
    if(reference->format!=SDL_PIXELFORMAT_RGBA32) {
        converted=SDL_ConvertSurface(reference,SDL_PIXELFORMAT_RGBA32);
        if(!converted)goto fail;
        SDL_DestroySurface(reference);reference=converted;converted=NULL;
    }
    if(reference->pitch!=reference->w*4 || art->pitch<=0 || reference->pitch<=0)goto fail;
    size_t bytes=((size_t)art->pitch+(size_t)reference->pitch)*(size_t)art->h;
    if(bytes>CPU_MATERIAL_BUDGET-cpu_material_bytes)goto fail;
    SDL_CloseIO(webp);SDL_CloseIO(pngfile);
    /* Publish only a complete pair. Every earlier failure leaves accounting
     * and all existing selections untouched, and retains canonical fallback. */
    cpu_material_cache[cpu_material_count++]=(CpuMaterialAtlas){
        key,art,reference,(unsigned)art->w/68,(unsigned)art->h/68,bytes};
    cpu_material_bytes+=bytes;CmCount(&cm_loaded);
    return 1;
fail:
    if(webp)SDL_CloseIO(webp);
    if(pngfile)SDL_CloseIO(pngfile);
    SDL_free(data);SDL_DestroySurface(converted);
    SDL_DestroySurface(art);SDL_DestroySurface(reference);return CpuMaterialRememberFailure(key);
}
static void CpuMaterialSelect(uint64_t key,unsigned cell) {
    cpu_material_next=(CpuMaterialSelection){0};
    if(!CpuMaterialEnabled())return;
    bool extension=(cell&0x800000u)!=0;cell&=0x7fffffu;
    for(unsigned i=0;i<cpu_material_count;++i) {
        CpuMaterialAtlas *a=&cpu_material_cache[i];
        if(a->key==key && cell<a->columns*a->rows) {
            cpu_material_next=(CpuMaterialSelection){a,cell,extension};CmCount(&cm_selected);return;
        }
    }
    CmCount(&cm_select_miss);
}
/* One selection belongs to the immediately following primitive, never UI later.
 * The draw backend will consume this at Draw_PushPrim entry, before decoding. */
static CpuMaterialSelection CpuMaterialTake(void) {
    CpuMaterialSelection result=cpu_material_next;
    cpu_material_next=(CpuMaterialSelection){0};
    return CpuMaterialEnabled()?result:(CpuMaterialSelection){0};
}
/* Pure consumer: caller must prove native accepted coverage first. All four
 * samples commit atomically. Refplanes identify unchanged palette pixels; they
 * do not confer PS1 coverage, depth, transparency or mask ownership. */
static bool CpuMaterialSample2x(CpuMaterialSelection selection,unsigned px,
    unsigned py,uint16_t native,uint16_t output[4]) {
    const CpuMaterialAtlas *a=selection.atlas;
    if(!a)return false;
    CmCount(&cm_samples);
    uint16_t staged[4];
    if(!a || !output || px>=16 || py>=16 || !native ||
        !a->columns || selection.cell>=a->columns*a->rows){CmCount(&cm_rejected);return false;}
    unsigned left=(selection.cell%a->columns)*68+2;
    unsigned top=(selection.cell/a->columns)*68+2;
    for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx) {
        unsigned x=left+px*4+sx*2+1,y=top+py*4+sy*2+1;
        const uint8_t *ref=(const uint8_t *)a->reference->pixels+
            (size_t)y*a->reference->pitch+(size_t)x*4;
        const uint8_t *art=(const uint8_t *)a->art->pixels+
            (size_t)y*a->art->pitch+(size_t)x*4;
        unsigned basis=(ref[0]>>3)|((ref[1]>>3)<<5)|((ref[2]>>3)<<10);
        if(ref[3]!=255 || art[3]!=255 || basis!=(native&0x7fff)){CmCount(&cm_rejected);return false;}
        staged[sy*2+sx]=(uint16_t)((art[0]>>3)|((art[1]>>3)<<5)|((art[2]>>3)<<10));
    }
    memcpy(output,staged,sizeof(staged));return true;
}
/* Extension art owns only noncanonical pixels. No reference comparison can
 * manufacture native coverage: absent/transparent texels simply draw nothing. */
static int CpuExtensionSample(unsigned px,unsigned py,unsigned lx,unsigned ly,
    unsigned scale,uint16_t *out) {
 const CpuMaterialSelection s=cpu_material_next;const CpuMaterialAtlas *a=s.atlas;
 if(!CpuMaterialEnabled() || !s.extension || !a || !out || px>=16 || py>=16 ||
    !scale || lx>=scale || ly>=scale || s.cell>=a->columns*a->rows)return 0;
 unsigned x=(s.cell%a->columns)*68+2+px*4+(4*lx+2)/scale;
 unsigned y=(s.cell/a->columns)*68+2+py*4+(4*ly+2)/scale;
 if(x>=(unsigned)a->art->w || y>=(unsigned)a->art->h)return 0;
 const uint8_t *v=(const uint8_t*)a->art->pixels+(size_t)y*a->art->pitch+x*4;
 if(v[3]!=255)return 0;
 *out=(uint16_t)((v[0]>>3)|((v[1]>>3)<<5)|((v[2]>>3)<<10));return 1;
}
#endif
