/* MPL-2.0. Independently authored native model replacement adapter.
 * xxHash dependency retains its BSD-2-Clause notice in duck_xxhash.h.
 * No DuckStation implementation is included. Index format is documented locally. */
#ifndef PSYZ_DUCK_MODELS_H
#define PSYZ_DUCK_MODELS_H
#include "duck_state_sha256.h"
#include <errno.h>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "duck_xxhash.h"
#define DUCK_MAX_ENTRIES 45000u
#define DUCK_RECORD_BYTES 282u
#define DUCK_MAX_MATCHES 128u
#define DUCK_MAX_PAGES 64u
#define DUCK_UPLOAD_BUDGET (16u*1024u*1024u)
typedef struct { unsigned short page,clut; } DuckPage;
typedef struct { unsigned entry; float x,y,w,h; int logged;
#if PE_NATIVE_STATE_CPU_COMPONENTS
 unsigned char *state_pixels; unsigned state_bytes,state_ax,state_ay,state_aw,state_ah; unsigned char state_sha[32];
#endif
} DuckImage;
static unsigned char *duck_index;
static unsigned duck_count,duck_pages_count,duck_image_count,duck_upload_bytes;
static DuckPage duck_pages[DUCK_MAX_PAGES];
static DuckImage duck_images[DUCK_MAX_MATCHES];
static unsigned short duck_words[1024*512];
static unsigned char duck_known[1024*512];
static unsigned long long duck_generation=1,duck_resolved_generation[DUCK_MAX_PAGES];
static int duck_enabled,duck_index_attempted,duck_bank=-1,duck_ever_submitted;
static char duck_root[1024]="/mnt/usb0/parasite-eve-data/mods/characters/duck-models";
static unsigned duck_match_log_budget=16,duck_hit_log_budget=24,duck_failure_log_budget=8,duck_submitted_polygons;
static int duck_image_failure;
static unsigned char duck_failed_entries[(DUCK_MAX_ENTRIES+7)/8];
static unsigned duck_state_reserved_cells;
/* Readonly compatibility generation: explicit successful reload/root changes
 * invalidate prior checkpoints before any new pixels can be reused. */
static uint32_t duck_pack_epoch=1;
static unsigned duck_arena_base,duck_arena_cursor_x,duck_arena_cursor_y,duck_arena_row_height;
#define DUCK_ARENA_ROWS 16u
static unsigned short DuckU16(const unsigned char *p){return (unsigned short)(p[0]|(p[1]<<8));}
static unsigned long long DuckU64(const unsigned char *p){unsigned long long v=0;for(unsigned i=0;i<8;i++)v|=(unsigned long long)p[i]<<(i*8);return v;}
static int DuckOverlap(int x,int y,int w,int h,int a,int b,int c,int d){return x<a+c&&a<x+w&&y<b+d&&b<y+h;}
static void DuckDirty(SDL_Rect r){
    if(r.x<0||r.y<0||r.w<=0||r.h<=0||r.x+r.w>1024||r.y+r.h>512)return;
    for(int y=0;y<r.h;y++)memset(duck_known+(r.y+y)*1024+r.x,0,(size_t)r.w);
    /* Generation only changes if a registered page/palette was affected. */
    for(unsigned i=0;i<duck_pages_count;i++){
        unsigned p=duck_pages[i].page,c=duck_pages[i].clut,mode=(p>>7)&3;
        int px=(p&15)*64,py=((p>>4)&1)*256,cx=(c&63)*16,cy=c>>6;
        if(DuckOverlap(r.x,r.y,r.w,r.h,px,py,mode==0?64:128,256)||DuckOverlap(r.x,r.y,r.w,r.h,cx,cy,mode==0?16:256,1))duck_resolved_generation[i]=0;
    }
    if(duck_generation!=~0ull)++duck_generation;
}
static void DuckUpload(SDL_Rect r,const unsigned short *words){
    if(r.x<0||r.y<0||r.w<=0||r.h<=0||r.x+r.w>1024||r.y+r.h>512)return;
    for(int y=0;y<r.h;y++){size_t o=(size_t)(r.y+y)*1024+r.x;memcpy(duck_words+o,words+(size_t)y*r.w,(size_t)r.w*2);memset(duck_known+o,1,(size_t)r.w);}
}
static void DuckAtlasReset(void){
#if PE_NATIVE_STATE_CPU_COMPONENTS
    for(unsigned i=0;i<duck_image_count;i++){SDL_free(duck_images[i].state_pixels);duck_images[i].state_pixels=NULL;}
#endif
    duck_state_reserved_cells=0;duck_image_count=0;duck_upload_bytes=0;duck_image_failure=0;memset(duck_failed_entries,0,sizeof duck_failed_entries);duck_arena_base=duck_arena_cursor_x=duck_arena_cursor_y=duck_arena_row_height=0;memset(duck_resolved_generation,0,sizeof duck_resolved_generation);}
static void DuckQuit(void){SDL_free(duck_index);duck_index=NULL;duck_count=0;duck_index_attempted=0;DuckAtlasReset();memset(duck_known,0,sizeof duck_known);duck_pages_count=0;}
int Psyz_GpuSetHdModels(int enabled,const char *root){
    if(root && root[0] && SDL_strcmp(root,duck_root)){
        if(SDL_strlen(root)>=sizeof duck_root)return -1;
        if(duck_pack_epoch==UINT32_MAX)return -1;
        ++duck_pack_epoch;
        SDL_strlcpy(duck_root,root,sizeof duck_root);SDL_free(duck_index);duck_index=NULL;duck_count=0;duck_index_attempted=0;
        /* Old atlas pixels remain allocated/immutable until normal room retirement. */
        unsigned reserved=duck_state_reserved_cells;DuckAtlasReset();duck_state_reserved_cells=reserved;
    }
    if(enabled&&!duck_enabled){memset(duck_failed_entries,0,sizeof duck_failed_entries);duck_image_failure=0;if(!duck_index){duck_index_attempted=0;memset(duck_resolved_generation,0,sizeof duck_resolved_generation);}}
    duck_enabled=!!enabled;
    SDL_Log("[HD models] requested=%d root=%s effective=identity-match-only source=registered-model-pages geometry=original",duck_enabled,duck_root);
    return 0;
}
const char *Psyz_GpuHdModelsStatus(void){
    if(!duck_enabled)return "Off";
    if(UseSoftwareBackend())return "Hardware renderer required";
    if(!scaled_vram_render||internal_res<=1)return "Needs render scale above 1x";
    if(duck_submitted_polygons)return "Replacement polygons submitted";
    if(duck_index_attempted&&!duck_index)return "Pack unavailable or invalid";
    if(!duck_index)return "Waiting for model texture draw";
    if(duck_image_failure)return "Matched image unavailable or over budget";
    return "Pack indexed; waiting for exact match";
}
/* Requested HD is independent of optional pack availability. A missing index
 * owns no replacement resources; retain genuine original-texture state. Lost
 * bound resources still fail closed, including after an earlier submission. */
static int DuckStateOriginalFallback(void){return !duck_index&&!duck_count&&!duck_image_count&&!duck_upload_bytes&&!duck_state_reserved_cells&&!duck_ever_submitted;}
int Psyz_GpuHdModelsStateSafe(void){return (duck_index&&duck_count)||DuckStateOriginalFallback();}
void Psyz_GpuBeginPlayerTextureSet(int bank){duck_bank=bank;duck_pages_count=0;memset(duck_resolved_generation,0,sizeof duck_resolved_generation);}
void Psyz_GpuRegisterPlayerTexturePage(unsigned page,unsigned clut){
    page&=511;clut&=32767;unsigned mode=(page>>7)&3;
    if(mode>1 || ((clut&63)*16+(mode==0?16:256)>1024))return;
    for(unsigned i=0;i<duck_pages_count;i++)if(duck_pages[i].page==page&&duck_pages[i].clut==clut)return;
    if(duck_pages_count<DUCK_MAX_PAGES)duck_pages[duck_pages_count++]=(DuckPage){(unsigned short)page,(unsigned short)clut};
}
/* Bounded transactional registration from source-owned actor model packets.
 * Same existing 64 descriptor state: no new pointer/cache lifetime. Return 1
 * only for complete admission; overflow never partially appends this actor. */
int Psyz_GpuRegisterActorTexturePages(const unsigned short *pairs,unsigned count,unsigned type,unsigned sub){
 if(!pairs||!count||count>DUCK_MAX_PAGES||type<1||type>9)return 0;
 DuckPage pending[DUCK_MAX_PAGES];unsigned added=0;
 for(unsigned j=0;j<count;j++){
  unsigned p=pairs[2*j],c=pairs[2*j+1];if(p>511||c>32767||((p>>7)&3)!=0||((c&63)*16+16>1024))return 0;
  unsigned k;for(k=0;k<duck_pages_count;k++)if(duck_pages[k].page==p&&duck_pages[k].clut==c)break;if(k<duck_pages_count)continue;
  for(k=0;k<added;k++)if(pending[k].page==p&&pending[k].clut==c)break;if(k<added)continue;
  if(duck_pages_count+added>=DUCK_MAX_PAGES){static unsigned overflowLog; if(overflowLog++<8)SDL_Log("[HD models] actor-pages rejected=capacity type=%u sub=%u existing=%u requested=%u original-unregistered-retained=1",type,sub,duck_pages_count,count);return 0;}
  pending[added++]=(DuckPage){(unsigned short)p,(unsigned short)c};
 }
 if(added){memcpy(duck_pages+duck_pages_count,pending,added*sizeof(*pending));duck_pages_count+=added;static unsigned admittedLog;if(admittedLog++<32)SDL_Log("[HD models] actor-pages admitted type=%u sub=%u added=%u total=%u replacement-hit-not-yet-observed=1",type,sub,added,duck_pages_count);}
 return 1;
}
static int DuckIndexReject(const char*stage,const char*path,unsigned char*data,size_t bytes,unsigned entry,int error){SDL_Log("[HD models] index-rejected stage=%s file=%s errno=%d bytes=%zu entry=%u error=%s original-retained=1",stage,path,error,bytes,entry,SDL_GetError());SDL_free(data);return 0;}
/* Bounded forward reads avoid relying on optional file seek/stat metadata. The
 * header fixes exact length; an additional EOF read rejects trailing bytes. */
static int DuckIndexRead(SDL_IOStream*io,unsigned char*out,size_t bytes,size_t*readBytes){size_t at=0;while(at<bytes){size_t n=SDL_ReadIO(io,out+at,bytes-at);if(!n){*readBytes=at;return 0;}at+=n;}*readBytes=at;return 1;}
static int DuckReadIndex(const char *root,unsigned char **output,unsigned *outputCount){
 char path[1200];if(SDL_snprintf(path,sizeof path,"%s/model-index.bin",root)>=(int)sizeof path)return DuckIndexReject("path-too-long",root,0,0,~0u,0);
 errno=0;SDL_IOStream*io=SDL_IOFromFile(path,"rb");if(!io){int error=errno;return DuckIndexReject("open",path,0,0,~0u,error);}unsigned char header[16];size_t got=0;errno=0;if(!DuckIndexRead(io,header,16,&got)){int error=errno;SDL_CloseIO(io);return DuckIndexReject("header-read",path,0,got,~0u,error);}
 unsigned count=(unsigned)header[8]|((unsigned)header[9]<<8)|((unsigned)header[10]<<16)|((unsigned)header[11]<<24);
 SDL_Log("[HD models] index-header magic=%02x%02x%02x%02x%02x%02x%02x%02x count=%u record=%u reserved=%u/%u size-method=bounded-read",header[0],header[1],header[2],header[3],header[4],header[5],header[6],header[7],count,DuckU16(header+12),header[14],header[15]);
 if(memcmp(header,"PEDUCK01",8)||DuckU16(header+12)!=DUCK_RECORD_BYTES||header[14]||header[15]||!count||count>DUCK_MAX_ENTRIES){SDL_CloseIO(io);return DuckIndexReject("header-contract",path,0,16,~0u,0);}
 size_t n=16+(size_t)count*DUCK_RECORD_BYTES;unsigned char*b=SDL_malloc(n);if(!b){int error=errno;SDL_CloseIO(io);return DuckIndexReject("allocate",path,0,n,~0u,error);}memcpy(b,header,16);errno=0;if(!DuckIndexRead(io,b+16,n-16,&got)){int error=errno;SDL_CloseIO(io);return DuckIndexReject("records-read",path,b,16+got,~0u,error);}
 unsigned char extra;errno=0;size_t trailing=SDL_ReadIO(io,&extra,1);SDL_IOStatus status=SDL_GetIOStatus(io);int error=errno;if(!SDL_CloseIO(io))return DuckIndexReject("close",path,b,n,~0u,errno);if(trailing||status!=SDL_IO_STATUS_EOF)return DuckIndexReject("length-or-read-error",path,b,n+trailing,~0u,error);
    for(unsigned i=0;i<count;i++){
        unsigned char *e=b+16+(size_t)i*DUCK_RECORD_BYTES;unsigned factor=e[0]==0?4:2;
        unsigned x=DuckU16(e+2),y=DuckU16(e+4),w=DuckU16(e+6),h=DuckU16(e+8);
        if(e[0]>1||e[1]>=(e[0]?256:16)||!w||!h||x+w>256||y+h>256||x%factor||w%factor||!e[26]||!memchr(e+26,0,256)){return DuckIndexReject("record",path,b,n,i,errno);}
        for(unsigned j=26;e[j];j++)if(!((e[j]>='a'&&e[j]<='z')||(e[j]>='A'&&e[j]<='Z')||(e[j]>='0'&&e[j]<='9')||e[j]=='-'||e[j]=='_'||e[j]=='.')){return DuckIndexReject("record",path,b,n,i,errno);}
        if(SDL_strstr((char *)e+26,"..")||!SDL_strstr((char *)e+26,".png")){return DuckIndexReject("record",path,b,n,i,errno);}
    }

 *output=b;*outputCount=count;SDL_Log("[HD models] index-staged entries=%u bytes=%zu pixels-rendered=0",count,n);return 1;
}
static int DuckLoadIndex(void){if(duck_index_attempted)return duck_index!=NULL;duck_index_attempted=1;return DuckReadIndex(duck_root,&duck_index,&duck_count);}
int Psyz_GpuReloadHdModels(const char *root){
    if(!root||!root[0]||SDL_strlen(root)>=sizeof duck_root||duck_pack_epoch==UINT32_MAX||!device||gpu_failed||UseSoftwareBackend())return -1;
    unsigned char *candidate=NULL;unsigned count=0;
    if(!DuckReadIndex(root,&candidate,&count)){SDL_Log("[HD models] reload-rejected stage=index old-pack-retained=1 epoch=%u",duck_pack_epoch);return -1;}
    /* Decode old copied packets before changing the source/cache authority.
     * Only a witnessed fence/idle permits retiring CPU decoded image copies. */
    Draw_FlushBuffer();
    if(Psyz_GpuExeque()<0||gpu_failed||n_vertices||n_indices||!SubmitCmdAndWait()||!SDL_WaitForGPUIdle(device)){SDL_free(candidate);SDL_Log("[HD models] reload-rejected stage=gpu-retirement old-pack-retained=1 epoch=%u",duck_pack_epoch);return -1;}
    unsigned reserved=duck_state_reserved_cells;
    DuckAtlasReset();duck_state_reserved_cells=reserved;
    SDL_free(duck_index);duck_index=candidate;duck_count=count;duck_index_attempted=1;
    SDL_strlcpy(duck_root,root,sizeof duck_root);++duck_pack_epoch;duck_submitted_polygons=0;
    duck_match_log_budget=16;duck_hit_log_budget=24;duck_failure_log_budget=8;
    SDL_Log("[HD models] reload-committed root=%s entries=%u epoch=%u old-atlas-cells-retained=%u pixels-rendered=0 preference-changed=0",duck_root,duck_count,duck_pack_epoch,reserved);
    return 0;
}
const char *Psyz_GpuHdModelsRoot(void){return duck_root;}
static int DuckHashRect(int x,int y,int w,int h,unsigned long long *out){
    if(x<0||y<0||w<=0||h<=0||x+w>1024||y+h>512)return 0;
    XXH3_state_t state;XXH3_64bits_reset(&state);unsigned char row[512];
    if(w>256)return 0;
    for(int j=0;j<h;j++){
        size_t o=(size_t)(y+j)*1024+x;
        for(int i=0;i<w;i++){if(!duck_known[o+i])return 0;unsigned v=duck_words[o+i];row[i*2]=(unsigned char)v;row[i*2+1]=(unsigned char)(v>>8);}
        XXH3_64bits_update(&state,row,(size_t)w*2);
    }
    *out=XXH3_64bits_digest(&state);return 1;
}
/* The existing atlas texture is shared. Reserve sixteen immutable new cell
 * rows once per room and shelf-pack model images inside them, with replicated
 * two-pixel gutters. Field cells continue after that reservation. */
/* Decode only a bounded immutable memory image. Native file streams need
 * neither seek nor GetIOSize; the decoder may seek inside its memory stream. */
static SDL_Surface *DuckLoadPngForward(const char *path){
    enum{limit=16*1024*1024,chunk=64*1024};
    errno=0;SDL_IOStream *io=SDL_IOFromFile(path,"rb");
    if(!io){SDL_Log("[HD models] png-rejected stage=open file=%s errno=%d error=%s",path,errno,SDL_GetError());return NULL;}
    unsigned char header[33];size_t got=0;
    if(!DuckIndexRead(io,header,sizeof header,&got)){SDL_CloseIO(io);SDL_Log("[HD models] png-rejected stage=header-read file=%s bytes=%zu",path,got);return NULL;}
    unsigned w=(unsigned)header[16]<<24|(unsigned)header[17]<<16|(unsigned)header[18]<<8|header[19];
    unsigned h=(unsigned)header[20]<<24|(unsigned)header[21]<<16|(unsigned)header[22]<<8|header[23];
    if(memcmp(header,"\x89PNG\r\n\x1a\n",8)||header[8]||header[9]||header[10]||header[11]!=13||memcmp(header+12,"IHDR",4)||!w||!h||w>2048||h>1024){SDL_CloseIO(io);SDL_Log("[HD models] png-rejected stage=header-contract file=%s dimensions=%ux%u",path,w,h);return NULL;}
    size_t cap=chunk,bytes=sizeof header;unsigned char *data=SDL_malloc(cap);if(!data){SDL_CloseIO(io);return NULL;}memcpy(data,header,bytes);
    int valid=1;
    for(;;){
        if(bytes==cap){
            if(cap==limit){unsigned char excess;size_t n=SDL_ReadIO(io,&excess,1);valid=!n&&SDL_GetIOStatus(io)==SDL_IO_STATUS_EOF;break;}
            size_t next=cap*2;if(next>limit)next=limit;void *grown=SDL_realloc(data,next);if(!grown){valid=0;break;}data=grown;cap=next;
        }
        size_t n=SDL_ReadIO(io,data+bytes,cap-bytes);bytes+=n;
        if(!n){valid=SDL_GetIOStatus(io)==SDL_IO_STATUS_EOF;break;}
    }
    if(!SDL_CloseIO(io))valid=0;
    if(!valid){SDL_Log("[HD models] png-rejected stage=bounded-read file=%s bytes=%zu limit=%u error=%s",path,bytes,limit,SDL_GetError());SDL_free(data);return NULL;}
    SDL_IOStream *memory=SDL_IOFromConstMem(data,bytes);SDL_Surface *image=memory?SDL_LoadPNG_IO(memory,true):NULL;
    if(!image)SDL_Log("[HD models] png-rejected stage=decode file=%s bytes=%zu error=%s",path,bytes,SDL_GetError());
    SDL_free(data);return image;
}
static DuckImage *DuckLoadImage(unsigned entry){
    for(unsigned i=0;i<duck_image_count;i++)if(duck_images[i].entry==entry)return &duck_images[i];
    if(duck_image_count>=DUCK_MAX_MATCHES || !device || gpu_failed)return NULL;
    const unsigned char *e=duck_index+16+(size_t)entry*DUCK_RECORD_BYTES;char path[1400];
    if(SDL_snprintf(path,sizeof path,"%s/%s",duck_root,e+26)>=(int)sizeof path)return NULL;
    SDL_Surface *source=DuckLoadPngForward(path);if(!source)return NULL;
    unsigned w=(unsigned)source->w,h=(unsigned)source->h,rw=DuckU16(e+6),rh=DuckU16(e+8);
    if(w<rw||h<rh||w>2048||h>2048||w%rw||h%rh||w/rw!=h/rh||w/rw>8){SDL_DestroySurface(source);return NULL;}
    SDL_Surface *rgba=SDL_ConvertSurface(source,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(source);if(!rgba)return NULL;
    unsigned aw=w+4,ah=h+4;
    unsigned start=duck_image_count?duck_arena_base:(sprite_atlas_cells+HD_ATLAS_COLUMNS-1)/HD_ATLAS_COLUMNS*HD_ATLAS_COLUMNS;
    unsigned sx=duck_arena_cursor_x,sy=duck_arena_cursor_y,rowh=duck_arena_row_height;
    if(sx+aw>HD_ATLAS_WIDTH){sx=0;sy+=rowh;rowh=0;}
    unsigned cells=DUCK_ARENA_ROWS*HD_ATLAS_COLUMNS;size_t bytes=(size_t)aw*ah*4;
    if(start>HD_ATLAS_COLUMNS*HD_ATLAS_ROWS||cells>HD_ATLAS_COLUMNS*HD_ATLAS_ROWS-start||aw>HD_ATLAS_WIDTH||sy+ah>DUCK_ARENA_ROWS*68||bytes>DUCK_UPLOAD_BUDGET-duck_upload_bytes){SDL_DestroySurface(rgba);return NULL;}
    /* Allocation is equivalent to the existing field-atlas allocation. */
    if(!sprite_atlas_texture){SDL_GPUTextureCreateInfo ti={.type=SDL_GPU_TEXTURETYPE_2D,.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER,.width=HD_ATLAS_WIDTH,.height=HD_ATLAS_HEIGHT,.layer_count_or_depth=1,.num_levels=1};sprite_atlas_texture=SDL_CreateGPUTexture(device,&ti);}
    if(!sprite_atlas_sampler){SDL_GPUSamplerCreateInfo si={.min_filter=SDL_GPU_FILTER_LINEAR,.mag_filter=SDL_GPU_FILTER_LINEAR,.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,.address_mode_u=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,.address_mode_v=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};sprite_atlas_sampler=SDL_CreateGPUSampler(device,&si);}
    SDL_GPUTransferBufferCreateInfo bi={.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,.size=(Uint32)bytes};
    SDL_GPUTransferBuffer *upload=sprite_atlas_texture&&sprite_atlas_sampler?SDL_CreateGPUTransferBuffer(device,&bi):NULL;
    unsigned char *mapped=upload?SDL_MapGPUTransferBuffer(device,upload,false):NULL;
    if(!mapped){if(upload)SDL_ReleaseGPUTransferBuffer(device,upload);SDL_DestroySurface(rgba);return NULL;}
    for(unsigned y=0;y<ah;y++)for(unsigned x=0;x<aw;x++){
        unsigned sx=x<2?0:x-2>=w?w-1:x-2,sy=y<2?0:y-2>=h?h-1:y-2;
        memcpy(mapped+((size_t)y*aw+x)*4,(unsigned char *)rgba->pixels+(size_t)sy*rgba->pitch+sx*4,4);
    }
#if PE_NATIVE_STATE_CPU_COMPONENTS
    unsigned char *state_pixels=SDL_malloc(bytes);
    if(!state_pixels){SDL_UnmapGPUTransferBuffer(device,upload);SDL_ReleaseGPUTransferBuffer(device,upload);SDL_DestroySurface(rgba);return NULL;}
    memcpy(state_pixels,mapped,bytes);
#endif
    SDL_UnmapGPUTransferBuffer(device,upload);SDL_DestroySurface(rgba);
    /* New immutable atlas rows cannot overlap any buffered draw. Copy commands precede the eventual draw submission; no vertex-buffer flush/rebase here. */
    SDL_GPUCommandBuffer *cmd=AcquireCmd();
    SDL_GPUCopyPass *copy=cmd?SDL_BeginGPUCopyPass(cmd):NULL;
    if(!copy){
#if PE_NATIVE_STATE_CPU_COMPONENTS
        SDL_free(state_pixels);
#endif
        SDL_ReleaseGPUTransferBuffer(device,upload);return NULL;}
    unsigned ax=sx,ay=(start/HD_ATLAS_COLUMNS)*68+sy;
    SDL_GPUTextureTransferInfo from={.transfer_buffer=upload,.pixels_per_row=aw,.rows_per_layer=ah};
    SDL_GPUTextureRegion to={.texture=sprite_atlas_texture,.x=ax,.y=ay,.w=aw,.h=ah,.d=1};
    SDL_UploadToGPUTexture(copy,&from,&to,false);SDL_EndGPUCopyPass(copy);SDL_ReleaseGPUTransferBuffer(device,upload);
    if(!duck_image_count){duck_state_reserved_cells+=start+cells-sprite_atlas_cells;sprite_atlas_cells=start+cells;duck_arena_base=start;}
    duck_arena_cursor_x=sx+aw;duck_arena_cursor_y=sy;duck_arena_row_height=ah>rowh?ah:rowh;duck_upload_bytes+=(unsigned)bytes;
    DuckImage *image=&duck_images[duck_image_count++];*image=(DuckImage){entry,(float)(ax+2),(float)(ay+2),(float)w,(float)h};
#if PE_NATIVE_STATE_CPU_COMPONENTS
    image->state_pixels=state_pixels;image->state_bytes=(unsigned)bytes;image->state_ax=ax;image->state_ay=ay;image->state_aw=aw;image->state_ah=ah;DuckDigest(state_pixels,bytes,image->state_sha);
#endif
    return image;
}
/* Cache exact matches for each source-owned page. Dirty sources invalidate the
 * page before the next polygon. Failed/unknown matches are bounded per epoch. */
static unsigned duck_match_entries[DUCK_MAX_PAGES][DUCK_MAX_MATCHES],duck_match_counts[DUCK_MAX_PAGES];
static int DuckPaletteCoverage(int px,int py,int x,int y,int w,int h,unsigned mode,unsigned hi){
    unsigned factor=mode?2:4,mask=mode?255:15,bits=mode?8:4;
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){unsigned word=duck_words[(py+y+j)*1024+px+(x+i)/(int)factor];unsigned value=(word>>(((x+i)%factor)*bits))&mask;if(value>hi)return 0;}
    return 1;
}
static void DuckResolve(unsigned slot){
    duck_match_counts[slot]=0;duck_resolved_generation[slot]=duck_generation;
    if(!DuckLoadIndex())return;
    unsigned p=duck_pages[slot].page,c=duck_pages[slot].clut,mode=(p>>7)&3,factor=mode?2:4;
    int px=(p&15)*64,py=((p>>4)&1)*256,cx=(c&63)*16,cy=c>>6;
    unsigned long long palettes[256];unsigned char palette_state[256]={0};unsigned examined=0;
    for(unsigned i=0;i<duck_count;i++){
        const unsigned char *e=duck_index+16+(size_t)i*DUCK_RECORD_BYTES;if(e[0]!=mode)continue;
        unsigned hi=e[1];if(!palette_state[hi])palette_state[hi]=DuckHashRect(cx,cy,(int)hi+1,1,&palettes[hi])?1:2;
        if(palette_state[hi]!=1||palettes[hi]!=DuckU64(e+18))continue;
        if(++examined>1024)break;
        unsigned long long hash;int x=DuckU16(e+2),y=DuckU16(e+4),w=DuckU16(e+6),h=DuckU16(e+8);
        if(!DuckHashRect(px+x/(int)factor,py+y,w/(int)factor,h,&hash)||hash!=DuckU64(e+10)||!DuckPaletteCoverage(px,py,x,y,w,h,mode,hi))continue;
        if(duck_match_counts[slot]==DUCK_MAX_MATCHES)break;
        duck_match_entries[slot][duck_match_counts[slot]++]=i;
    }
    if(duck_match_log_budget){--duck_match_log_budget;SDL_Log("[HD models] bank-hint=%d page=%u clut=%u exact-matches=%u palette-candidates=%u rendered=0",duck_bank,p,c,duck_match_counts[slot],examined);}
}
static void DuckApply(Vertex *v,unsigned n,unsigned page,unsigned clut){
    if(!duck_enabled||UseSoftwareBackend()||!scaled_vram_render||internal_res<=1||cur_twin!=TWIN_PACK(255,255,0,0))return;
    unsigned slot;for(slot=0;slot<duck_pages_count;slot++)if(duck_pages[slot].page==(page&511)&&duck_pages[slot].clut==clut)break;
    if(slot==duck_pages_count)return;
    if(!duck_resolved_generation[slot])DuckResolve(slot);
    /* Prefer the most specific exact-source rectangle containing every UV.
     * Equal-area alternatives remain ambiguous, independent of file ordering. */
    unsigned selected=~0u,best_area=~0u;int ambiguous=0;
    for(unsigned m=0;m<duck_match_counts[slot];m++){
        unsigned i=duck_match_entries[slot][m];const unsigned char *e=duck_index+16+(size_t)i*DUCK_RECORD_BYTES;
        float x=DuckU16(e+2),y=DuckU16(e+4),w=DuckU16(e+6),h=DuckU16(e+8);int inside=1;
        for(unsigned j=0;j<n;j++)if(v[j].u<x||v[j].u>=x+w||v[j].v<y||v[j].v>=y+h)inside=0;
        if(inside){unsigned area=(unsigned)(w*h);if(area<best_area){selected=i;best_area=area;ambiguous=0;}else if(area==best_area)ambiguous=1;}
    }
    if(selected==~0u||ambiguous)return;
    /* New immutable atlas rows do not flush or rebase the pending polygon. */
    if(duck_failed_entries[selected/8]&(1u<<(selected%8)))return;
    DuckImage *image=DuckLoadImage(selected);if(!image){duck_image_failure=1;duck_failed_entries[selected/8]|=(unsigned char)(1u<<(selected%8));if(duck_failure_log_budget){--duck_failure_log_budget;SDL_Log("[HD models] matched-image-rejected file=%s original-retained=1",(const char *)duck_index+16+(size_t)selected*DUCK_RECORD_BYTES+26);}return;}
    const unsigned char *e=duck_index+16+(size_t)selected*DUCK_RECORD_BYTES;
    for(unsigned j=0;j<n;j++){
        v[j].hd_uv[0]=(image->x+(v[j].u-DuckU16(e+2))*image->w/DuckU16(e+6))/HD_ATLAS_WIDTH;
        v[j].hd_uv[1]=(image->y+(v[j].v-DuckU16(e+4))*image->h/DuckU16(e+8))/HD_ATLAS_HEIGHT;
        v[j].hd_uv[2]=5;
    }
    duck_ever_submitted=1;if(duck_submitted_polygons!=~0u)++duck_submitted_polygons;
    if(!image->logged&&duck_hit_log_budget){image->logged=1;--duck_hit_log_budget;SDL_Log("[HD models] replacement-polygon-submitted bank-hint=%d page=%u clut=%u file=%s vertices=%u gpu-pixels=human-unverified",duck_bank,page&511,clut,(const char *)e+26,n);}
}
#if PE_NATIVE_STATE_CPU_COMPONENTS
static bool SubmitCmdAndWait(void);
/* Explicit checkpoint payload: no raw structs, pointers, cache arenas or paths.
 * Upload-known bits preserve source provenance. Native GPU bytes are separately
 * authoritative; never mark render-written/unknown words as uploaded originals. */
#define DUCK_STATE_BYTES (128u+DUCK_MAX_PAGES*4u+1024u*512u/8u)
static unsigned DuckStateGet32(const unsigned char *p){return(unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static void DuckStatePut32(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static void DuckStateHashU32(DuckSha256 *s,unsigned v){unsigned char b[4];DuckStatePut32(b,v);DuckSha_update(s,b,4);}
static unsigned DuckStateFieldCells(void){return sprite_atlas_cells>=duck_state_reserved_cells?sprite_atlas_cells-duck_state_reserved_cells:~0u;}
static int DuckStateImagePrefix(unsigned count,unsigned char digest[32]){
    if(count>duck_image_count||duck_image_count>DUCK_MAX_MATCHES||duck_upload_bytes>DUCK_UPLOAD_BUDGET)return 0;
    DuckSha256 s;DuckSha_init(&s);unsigned bytes=0;
    for(unsigned i=0;i<count;i++){
        const DuckImage *im=&duck_images[i];unsigned char sha[32];
        if(!duck_index||im->entry>=duck_count||!im->state_pixels||!im->state_aw||!im->state_ah||
           im->state_aw>HD_ATLAS_WIDTH||im->state_ah>DUCK_ARENA_ROWS*68u||
           im->state_ax>HD_ATLAS_WIDTH-im->state_aw||im->state_ay>HD_ATLAS_HEIGHT/2-im->state_ah||
           im->state_bytes!=(size_t)im->state_aw*im->state_ah*4||im->state_bytes>DUCK_UPLOAD_BUDGET-bytes)return 0;
        bytes+=im->state_bytes;DuckDigest(im->state_pixels,im->state_bytes,sha);
        if(memcmp(sha,im->state_sha,32))return 0;
        DuckStateHashU32(&s,im->entry);DuckStateHashU32(&s,im->state_ax);DuckStateHashU32(&s,im->state_ay);
        DuckStateHashU32(&s,im->state_aw);DuckStateHashU32(&s,im->state_ah);DuckSha_update(&s,sha,32);
    }
    DuckSha_final(&s,digest);return 1;
}
static int DuckStateIdentity(unsigned char out[128],unsigned prefix){
    if(!Psyz_GpuHdModelsStateSafe()||duck_pages_count>DUCK_MAX_PAGES||DuckStateFieldCells()==~0u)return 0;
    memset(out,0,128);memcpy(out,"PEDUCK03",8);DuckStatePut32(out+8,(unsigned)duck_enabled);
    DuckStatePut32(out+12,(unsigned)duck_bank);DuckStatePut32(out+16,duck_pages_count);DuckStatePut32(out+20,duck_count);DuckStatePut32(out+24,prefix);DuckStatePut32(out+28,duck_pack_epoch);
    if(duck_index)DuckDigest(duck_index,16+(size_t)duck_count*DUCK_RECORD_BYTES,out+32);
    DuckDigest(duck_root,SDL_strlen(duck_root)+1,out+64);
    return DuckStateImagePrefix(prefix,out+96);
}
static int DuckStateValidate(const unsigned char *in,size_t bytes){
    unsigned char exact[128];if(!in||bytes!=DUCK_STATE_BYTES||memcmp(in,"PEDUCK03",8))return 0;
    unsigned pages=DuckStateGet32(in+16),prefix=DuckStateGet32(in+24);
    if(pages>DUCK_MAX_PAGES||!DuckStateIdentity(exact,prefix))return 0;
    unsigned char allImages[32];if(prefix!=duck_image_count&&!DuckStateImagePrefix(duck_image_count,allImages))return 0;
    /* Bank/page descriptors are restored; all other identity fields readonly. */
    memcpy(exact+12,in+12,8);
    if(memcmp(in,exact,128))return 0;
    for(unsigned i=0;i<DUCK_MAX_PAGES;i++){
        const unsigned char *p=in+128+i*4;unsigned page=DuckU16(p),clut=DuckU16(p+2),mode=(page>>7)&3;
        if(i>=pages){if(page||clut)return 0;continue;}
        if(page>511||clut>32767||mode>1||(clut&63)*16+(mode?256:16)>1024)return 0;
        for(unsigned j=0;j<i;j++)if(!memcmp(p,in+128+j*4,4))return 0;
    }
    return 1;
}
static int DuckStateCapture(unsigned char *out,size_t bytes,const unsigned char *rgba,size_t rgbaBytes){
    if(!out||bytes!=DUCK_STATE_BYTES||!rgba||rgbaBytes!=1024u*512u*4u||!DuckStateIdentity(out,duck_image_count))return 0;
    static unsigned fallbackLogs;if(DuckStateOriginalFallback()&&fallbackLogs<4){++fallbackLogs;SDL_Log("[native checkpoint] HD-owner=original-fallback requested=%d index-bound=0 replacement-images=0 optional-pack-required=0",duck_enabled);}
    memset(out+128,0,DUCK_STATE_BYTES-128);
    for(unsigned i=0;i<duck_pages_count;i++){out[128+i*4]=(unsigned char)duck_pages[i].page;out[129+i*4]=(unsigned char)(duck_pages[i].page>>8);out[130+i*4]=(unsigned char)duck_pages[i].clut;out[131+i*4]=(unsigned char)(duck_pages[i].clut>>8);}
    unsigned char *known=out+128+DUCK_MAX_PAGES*4;
    for(unsigned i=0;i<1024u*512u;i++)if(duck_known[i]){
        unsigned short word;ConvertRgba8888ToRgb5551(rgba+(size_t)i*4,&word,1);
        if(word!=duck_words[i])return 0; /* Missing dirty provenance fails closed. */
        known[i/8]|=(unsigned char)(1u<<(i%8));
    }
    return DuckStateValidate(out,bytes);
}
static int DuckStateReuploadImages(void){
    /* Only called after the native state GPU fence with zero pending vertices.
     * Reupload the same immutable decoded bytes to their SAME reserved cells.
     * Never rewind allocator/overwrite field cells/reopen PNGs from storage. */
    if(duck_image_count&&(!sprite_atlas_texture||!sprite_atlas_sampler))return 0;
    unsigned char digest[32];if(!DuckStateImagePrefix(duck_image_count,digest))return 0;
    for(unsigned i=0;i<duck_image_count;i++){
        const DuckImage *im=&duck_images[i];SDL_GPUTransferBufferCreateInfo bi={.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,.size=im->state_bytes};
        SDL_GPUTransferBuffer *upload=SDL_CreateGPUTransferBuffer(device,&bi);if(!upload)return 0;
        void *p=SDL_MapGPUTransferBuffer(device,upload,false);if(!p){SDL_ReleaseGPUTransferBuffer(device,upload);return 0;}
        memcpy(p,im->state_pixels,im->state_bytes);SDL_UnmapGPUTransferBuffer(device,upload);
        SDL_GPUCommandBuffer *cmd=AcquireCmd();SDL_GPUCopyPass *copy=cmd?SDL_BeginGPUCopyPass(cmd):NULL;
        if(!copy){SDL_ReleaseGPUTransferBuffer(device,upload);return 0;}
        SDL_GPUTextureTransferInfo from={.transfer_buffer=upload,.pixels_per_row=im->state_aw,.rows_per_layer=im->state_ah};
        SDL_GPUTextureRegion to={.texture=sprite_atlas_texture,.x=im->state_ax,.y=im->state_ay,.w=im->state_aw,.h=im->state_ah,.d=1};
        SDL_UploadToGPUTexture(copy,&from,&to,false);SDL_EndGPUCopyPass(copy);SDL_ReleaseGPUTransferBuffer(device,upload);
    }
    return SubmitCmdAndWait()&&SDL_WaitForGPUIdle(device)&&!gpu_failed;
}
static int DuckStateRestore(const unsigned char *in,size_t bytes,const unsigned char *rgba,size_t rgbaBytes){
    if(!rgba||rgbaBytes!=1024u*512u*4u||!DuckStateValidate(in,bytes)||!DuckStateReuploadImages())return 0;
    /* The caller has already restored VRAM/MarkVramDirty. This must be LAST,
     * otherwise MarkVramDirty would immediately erase restored provenance. */
    ConvertRgba8888ToRgb5551(rgba,duck_words,1024u*512u);
    const unsigned char *known=in+128+DUCK_MAX_PAGES*4;
    for(unsigned i=0;i<1024u*512u;i++)duck_known[i]=(known[i/8]>>(i%8))&1;
    duck_bank=(int32_t)DuckStateGet32(in+12);duck_pages_count=DuckStateGet32(in+16);
    for(unsigned i=0;i<duck_pages_count;i++)duck_pages[i]=(DuckPage){DuckU16(in+128+i*4),DuckU16(in+130+i*4)};
    memset(duck_resolved_generation,0,sizeof duck_resolved_generation);memset(duck_match_counts,0,sizeof duck_match_counts);
    memset(duck_failed_entries,0,sizeof duck_failed_entries);duck_image_failure=0;duck_generation=duck_generation==~0ull?1:duck_generation+1;
    return 1;
}
#endif

#endif
