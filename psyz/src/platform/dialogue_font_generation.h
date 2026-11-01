/* Original bounded native-font write observer. Tokens are cache hints, never
 * a replacement for exact source-font reference comparison after dirtying. */
typedef struct{SDL_Rect pixels,palette;uint64_t token;unsigned page,clut;int bound,overflow;} NativeFontWatch;
static NativeFontWatch dialogue_watches[2]={{.token=1},{.token=1}};
static void DialogueFontBump(unsigned context){if(dialogue_watches[context].token==UINT64_MAX)dialogue_watches[context].overflow=1;else dialogue_watches[context].token++;}
void Psyz_GpuInvalidateDialogueFont(void){for(unsigned context=0;context<2;context++)DialogueFontBump(context);}
static void DialogueFontWatchReset(void){for(unsigned context=0;context<2;context++){dialogue_watches[context].bound=0;DialogueFontBump(context);}}
static int DialogueFontIntersects(SDL_Rect a,SDL_Rect b){return a.w>0&&a.h>0&&b.w>0&&b.h>0&&(int64_t)a.x+a.w>b.x&&(int64_t)b.x+b.w>a.x&&(int64_t)a.y+a.h>b.y&&(int64_t)b.y+b.h>a.y;}
static void DialogueFontDirty(SDL_Rect changed){for(unsigned context=0;context<2;context++)if(dialogue_watches[context].bound&&(DialogueFontIntersects(changed,dialogue_watches[context].pixels)||DialogueFontIntersects(changed,dialogue_watches[context].palette)))DialogueFontBump(context);}
uint64_t Psyz_GpuWatchFontContext(unsigned context,unsigned page,unsigned clut){
    if(context>=2||page>65535||(page&0x180)||clut>65535||!device||UseSoftwareBackend()||gpu_failed||dialogue_watches[context].overflow)return 0;
    SDL_Rect pixels={(int)(page&15)*64,(page&16)?256:0,64,48},palette={(int)(clut%64)*16,(int)(clut/64),16,1};
    if(pixels.x+pixels.w>VRAM_W||pixels.y+pixels.h>VRAM_H||palette.x+palette.w>VRAM_W||palette.y>=VRAM_H)return 0;
    if(!dialogue_watches[context].bound||dialogue_watches[context].page!=page||dialogue_watches[context].clut!=clut){dialogue_watches[context].pixels=pixels;dialogue_watches[context].palette=palette;dialogue_watches[context].page=page;dialogue_watches[context].clut=clut;dialogue_watches[context].bound=1;DialogueFontBump(context);}
    return dialogue_watches[context].overflow?0:dialogue_watches[context].token;
}
uint64_t Psyz_GpuFontContextGeneration(unsigned context,unsigned page,unsigned clut){
    if(!Psyz_GpuWatchFontContext(context,page,clut))return 0;
    /* Drain native command bookkeeping, never wait/download on stable frames.
     * Flush first: GPU_Enqueue's executor resets its direct vertex buffer. */
    Draw_FlushBuffer();if(Psyz_GpuExeque()<0||gpu_failed)return 0;
    return dialogue_watches[context].overflow?0:dialogue_watches[context].token;
}

uint64_t Psyz_GpuWatchDialogueFont(unsigned page,unsigned clut){return Psyz_GpuWatchFontContext(0,page,clut);}
uint64_t Psyz_GpuDialogueFontGeneration(unsigned page,unsigned clut){return Psyz_GpuFontContextGeneration(0,page,clut);}
