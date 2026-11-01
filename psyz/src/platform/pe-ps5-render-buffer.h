/* SPDX-License-Identifier: MIT */
#ifndef PE_PS5_RENDER_BUFFER_H
#define PE_PS5_RENDER_BUFFER_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef PE_PS5
extern int sceKernelAllocateMainDirectMemory(size_t,size_t,int,intptr_t*);
extern int sceKernelMapDirectMemory(void**,size_t,int,int,intptr_t,size_t);
extern int sceKernelReleaseDirectMemory(intptr_t,size_t);
extern int sceKernelMunmap(void*,size_t);
typedef struct {void *data;size_t bytes;intptr_t physical;bool allocated;} PeRenderBuffer;
static void PeRenderError(const char *stage,int error,size_t bytes){
 static unsigned reports;
 if(reports++<16)ERRORF("PS5 geometry direct memory %s failed code=0x%x bytes=%zu",stage,(unsigned)error,bytes);
}
static bool PeRenderRelease(PeRenderBuffer *b){
 if(b->data){int r=sceKernelMunmap(b->data,b->bytes);if(r){PeRenderError("unmap",r,b->bytes);return false;}b->data=NULL;}
 if(b->allocated){int r=sceKernelReleaseDirectMemory(b->physical,b->bytes);if(r){PeRenderError("release",r,b->bytes);return false;}b->allocated=false;}
 b->bytes=0;b->physical=0;return true;
}
/* Fixed bounded arenas, allocated once on first use. Never grow or replace a
 * live mapping. A failed cleanup retains its handle for teardown retry. */
static bool PeRenderReserve(PeRenderBuffer *b,size_t bytes){
 const size_t align=0x20000,limit=24u*1024u*1024u;
 if(!bytes || bytes>limit)return false;
 if(b->data)return b->bytes>=bytes;
 if(b->allocated)return false;
 bytes=(bytes+align-1)&~(align-1);
 intptr_t physical=0;
 int r=sceKernelAllocateMainDirectMemory(bytes,align,3,&physical);
 if(r){PeRenderError("allocate",r,bytes);return false;}
 b->physical=physical;b->bytes=bytes;b->allocated=true;
 void *mapped=NULL;r=sceKernelMapDirectMemory(&mapped,bytes,0x33,0,physical,align);
 if(r || !mapped){PeRenderError("map",r?r:-1,bytes);PeRenderRelease(b);return false;}
 b->data=mapped;INFOF("PS5 geometry direct memory ready bytes=%zu",bytes);return true;
}
#endif
#endif
