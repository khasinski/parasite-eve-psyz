/* SPDX-License-Identifier: MIT
 * Bounded variable-scale CPU triangle component (1 native; 2/4/6/8 experimental). No canonical VRAM writes or feedback.
 * Caller seeds a presentation epoch from canonical display-page RGB555.
 * Unsupported operation poisons the WHOLE epoch until next explicit begin.
 * Configuration and presentation are owned by the caller. */
#ifndef PE_CPU_2X_H
#define PE_CPU_2X_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pe-ps5-render-buffer.h"
#include <stdlib.h>
#include <limits.h>
/* Entry points stay in the object even when nothing in it calls them (the
 * linker's section GC). MSVC keeps external functions without help. */
#if defined(__GNUC__) || defined(__clang__)
#if defined(__has_attribute)
#if __has_attribute(retain)
#define PE2_KEEP __attribute__((used, retain))
#endif
#endif
#ifndef PE2_KEEP
#define PE2_KEEP __attribute__((used))
#endif
#else
#define PE2_KEEP
#endif
#define PE2_MAX_SCALE 8
#define PE2_MAX_PIXELS ((size_t)320*240*PE2_MAX_SCALE*PE2_MAX_SCALE)
static bool Pe2PageValid(int x,int y,int w,int h){
 return x>=0 && y>=0 && w>0 && h>0 && w<=320 && h<=240 && x<=1024-w && y<=512-h;
}
enum {PE2_UNSUPPORTED=1,PE2_BOUNDS,PE2_ALLOC,PE2_PAGE,PE2_CONFIG,PE2_RGB24,PE2_EMPTY,PE2_REASON_COUNT};
static bool Pe2ScaleValid(int s){return s==1 || s==2 || s==4 || s==6 || s==8;}
_Static_assert(320*8*3 <= INT_MAX && 240*8 <= INT_MAX,"RGB pitch/dimensions");
_Static_assert(PE2_MAX_PIXELS <= SIZE_MAX/3,"allocation arithmetic");
_Static_assert(INT64_C(255)*4095*4096*2*16*8192*2 < INT64_MAX,"attribute intermediates");
/* Floor, including negative attribute planes; preserves native 8-bit wrap. */
static int Pe2Attribute(int64_t n,int d){
 int64_t q=n/d;if(n<0 && n%d)--q;return (int)((uint64_t)q&255);
}
typedef struct {int x,y,u,v,r,g,b;} Pe2Vertex;
#ifndef PE2_SAMPLE_VISIBLE
#define PE2_SAMPLE_VISIBLE(value) ((value)!=0)
#endif
typedef uint16_t (*Pe2Sample)(void *,int,int,int,int,int);
#ifndef PE2_MATERIAL_USED
#define PE2_MATERIAL_USED false
#endif
typedef uint16_t (*Pe2PolygonSample)(void *,int,int,int,int,int,int,int);
/* Per-primitive state is captured before canonical rasterization. Destination
 * words retain RGB555 and bit15 independently at every high-resolution sample. */
typedef struct {
 bool semi,set_mask,preserve_mask;
 int abr;
 bool (*write)(uint16_t *,uint16_t,bool,int,bool,bool);
} Pe2Paint;
static bool Pe2Write(Pe2Paint paint,uint16_t *out,uint16_t src,bool textured) {
 return paint.write(out,src,paint.semi && (!textured || (src&0x8000)),
                    paint.abr,paint.set_mask,paint.preserve_mask);
}
typedef struct {
 uint16_t *pixels;
#ifdef PE_PS5
 PeRenderBuffer direct;
#endif
 size_t capacity;
 unsigned long long material_covered,material_total;
 int scale,reason;
 int w,h,page_x,page_y,sx0,sy0,sx1,sy1;
 bool active,failed,drawn;
} Pe2Frame;
static void Pe2RejectReason(Pe2Frame *f,int reason){
 if(f && f->active){if(!f->failed)f->reason=reason;f->failed=true;}
}
PE2_KEEP void Pe2Reject(Pe2Frame *f){Pe2RejectReason(f,PE2_UNSUPPORTED);}
/* Bounds and page identity are explicit. No backing pointer escapes. */
PE2_KEEP bool Pe2Begin(Pe2Frame *f,int scale,const uint16_t *native,int pitch,
 int x,int y,int w,int h) {
 if(!f)return false;
 f->active=false;f->failed=false;f->drawn=false;f->reason=0;f->scale=scale;
 f->material_covered=f->material_total=0;
 if(!Pe2ScaleValid(scale) || !native || pitch!=1024 || !Pe2PageValid(x,y,w,h)){f->reason=PE2_BOUNDS;return false;}
 size_t count=(size_t)w*h*scale*scale;
 if(count>PE2_MAX_PIXELS){f->reason=PE2_BOUNDS;return false;}
#ifdef PE_PS5
 if(!PeRenderReserve(&f->direct,PE2_MAX_PIXELS*sizeof(uint16_t))){f->reason=PE2_ALLOC;return false;}
 f->pixels=f->direct.data;f->capacity=PE2_MAX_PIXELS;
#else
 if(count>f->capacity){
  uint16_t *next=realloc(f->pixels,count*sizeof(*next));
  if(!next){f->reason=PE2_ALLOC;return false;}
  f->pixels=next;f->capacity=count;
 }
#endif
 f->w=w*f->scale;f->h=h*f->scale;f->page_x=x;f->page_y=y;
 f->sx0=0;f->sy0=0;f->sx1=f->w;f->sy1=f->h;
 for(int py=0;py<f->h;++py)for(int px=0;px<f->w;++px)
  f->pixels[py*f->w+px]=native[(y+py/f->scale)*pitch+x+px/f->scale];
 f->active=true;return true;
}
PE2_KEEP void Pe2Scissor(Pe2Frame *f,int x0,int y0,int x1,int y1){
 if(!f || !f->active)return;
 if(x0 < -2048 || x0>2047 || y0 < -2048 || y0>2047 ||
    x1 < -2048 || x1>2047 || y1 < -2048 || y1>2047){Pe2Reject(f);return;}
 /* Native draw area is inclusive. Restrict it to the selected display page. */
 int a=(x0-f->page_x)*f->scale,b=(y0-f->page_y)*f->scale;
 int c=(x1-f->page_x+1)*f->scale,d=(y1-f->page_y+1)*f->scale;
 f->sx0=a<0?0:(a>f->w?f->w:a);f->sy0=b<0?0:(b>f->h?f->h:b);
 f->sx1=c<0?0:(c>f->w?f->w:c);f->sy1=d<0?0:(d>f->h?f->h:d);
}
/* Direct VRAM mutation ownership is independent of draw-area scissor.
 * Load/move deliberately replace detail with canonical words: no shadow feedback.
 * Source must be the already-mutated canonical row, preserving native row-order
 * memmove overlap semantics even for vertically overlapping rectangles. */
static void Pe2CanonicalSpan(Pe2Frame *f,int x,int y,int w,const uint16_t *row){
 if(!f || !f->active || f->failed || y<f->page_y || y>=f->page_y+f->h/f->scale)return;
 int lo=x<f->page_x?f->page_x:x;
 int hi=x+w;if(hi>f->page_x+f->w/f->scale)hi=f->page_x+f->w/f->scale;
 for(int nx=lo;nx<hi;++nx){
  int px=(nx-f->page_x)*f->scale,py=(y-f->page_y)*f->scale;
  uint16_t c=row[nx-x];
  for(int ly=0;ly<f->scale;++ly)for(int lx=0;lx<f->scale;++lx)
   f->pixels[(py+ly)*f->w+px+lx]=c;
  f->drawn=true;
 }
}
/* Native clear ignores draw scissor and blending but honors preserve-mask;
 * apply that decision to each independent destination, including bit15. */
static void Pe2ClearSpan(Pe2Frame *f,int x,int y,int w,uint16_t color,bool preserve){
 if(!f || !f->active || f->failed || y<f->page_y || y>=f->page_y+f->h/f->scale)return;
 int lo=x<f->page_x?f->page_x:x;
 int hi=x+w;if(hi>f->page_x+f->w/f->scale)hi=f->page_x+f->w/f->scale;
 for(int nx=lo;nx<hi;++nx)for(int lane_y=0;lane_y<f->scale;++lane_y)
  for(int lane_x=0;lane_x<f->scale;++lane_x){
   uint16_t *out=&f->pixels[((y-f->page_y)*f->scale+lane_y)*f->w+(nx-f->page_x)*f->scale+lane_x];
   if(!preserve || !(*out&0x8000)){*out=color;f->drawn=true;}
  }
}
/* Supplied by the canonical backend: matrix and RGB555 clamp order shared. */
static int Pe2Dither(int x,int y);
static uint16_t Pe2PackDither(int r,int g,int b,int d);
static int64_t Pe2Edge(Pe2Vertex a,Pe2Vertex b,int x4,int y4,int denom){
 return (int64_t)(b.x-a.x)*(y4-a.y*denom)-(int64_t)(b.y-a.y)*(x4-a.x*denom);
}
static bool Pe2Top(Pe2Vertex a,Pe2Vertex b){return b.y<a.y || (b.y==a.y && b.x>a.x);}
/* Dedicated native centered line triangles: RGB uses exact barycentric
 * truncation, never the polygon's anchor-biased fixed-point attribute planes.
 * Ordered triangles share the same top-left rule, so their diagonal has one owner. */
static bool Pe2CenteredLineTriangle(Pe2Frame *f,Pe2Vertex a,Pe2Vertex b,Pe2Vertex c,
 bool do_dither,Pe2Paint paint){
 if(!f || !f->active || f->failed)return false;
 /* VertexXY may include draw offset and the signed endpoint extension.
  * Its int range is bounded by native Sign11 plus native draw offset. */
 int64_t area=(int64_t)(b.x-a.x)*(c.y-a.y)-(int64_t)(b.y-a.y)*(c.x-a.x);
 if(!area)return true;
 if(area<0){Pe2Vertex t=b;b=c;c=t;area=-area;}
 int minx=a.x<b.x?a.x:b.x;if(c.x<minx)minx=c.x;
 int maxx=a.x>b.x?a.x:b.x;if(c.x>maxx)maxx=c.x;
 int miny=a.y<b.y?a.y:b.y;if(c.y<miny)miny=c.y;
 int maxy=a.y>b.y?a.y:b.y;if(c.y>maxy)maxy=c.y;
 minx=(minx-f->page_x)*f->scale;maxx=(maxx-f->page_x)*f->scale;
 miny=(miny-f->page_y)*f->scale;maxy=(maxy-f->page_y)*f->scale;
 if(minx<f->sx0)minx=f->sx0;if(miny<f->sy0)miny=f->sy0;
 if(maxx>f->sx1)maxx=f->sx1;if(maxy>f->sy1)maxy=f->sy1;
 for(int y=miny;y<maxy;++y)for(int x=minx;x<maxx;++x){
  int units=2*f->scale;
  int x4=f->page_x*units+x*2+1,y4=f->page_y*units+y*2+1;
  int64_t wa=Pe2Edge(b,c,x4,y4,units),wb=Pe2Edge(c,a,x4,y4,units),wc=Pe2Edge(a,b,x4,y4,units);
  if(wa<0 || (!wa&&!Pe2Top(b,c)) || wb<0 || (!wb&&!Pe2Top(c,a)) ||
     wc<0 || (!wc&&!Pe2Top(a,b)))continue;
  int64_t denom=area*units;
  int r=(int)((wa*a.r+wb*b.r+wc*c.r)/denom);
  int g=(int)((wa*a.g+wb*b.g+wc*c.g)/denom);
  int blue=(int)((wa*a.b+wb*b.b+wc*c.b)/denom);
  int d=do_dither?Pe2Dither(f->page_x+x/f->scale,f->page_y+y/f->scale):0;
  uint16_t color=Pe2PackDither(r,g,blue,d);
  if(Pe2Write(paint,&f->pixels[y*f->w+x],color,false))f->drawn=true;
 }
 return true;
}
/* Flat/Gouraud, untextured/raw/modulated triangles. Vertex coords
 * remain integer PS1 positions. Coverage is evaluated at (2*lane+1)/(2*scale) native pixels,
 * native-biased 12-bit UV gradients at each covered subpixel; never sampled from an upscaled
 * final image. Canonical sampler MUST be read-only and alias-free for epoch.
 * Aliasing, lines and VRAM mutations
 * require Pe2Reject; native raster remains the sole feedback authority. */
PE2_KEEP bool Pe2Triangle(Pe2Frame *f,Pe2Vertex a,Pe2Vertex b,Pe2Vertex c,
 bool gouraud,bool do_dither,Pe2PolygonSample sample,void *context,Pe2Paint paint,bool unsupported){
 if(!f || !f->active || f->failed)return false;
 if(unsupported){Pe2Reject(f);return false;}
 Pe2Vertex v[3]={a,b,c};
 for(int i=0;i<3;++i)if(v[i].x < -2048 || v[i].x>2047 ||
  v[i].y < -2048 || v[i].y>2047 || v[i].u<0 || v[i].u>255 ||
  v[i].v<0 || v[i].v>255 || v[i].r<0 || v[i].r>255 ||
  v[i].g<0 || v[i].g>255 || v[i].b<0 || v[i].b>255){Pe2Reject(f);return false;}
 int64_t area=(int64_t)(b.x-a.x)*(c.y-a.y)-(int64_t)(b.y-a.y)*(c.x-a.x);
 if(!area)return true;
 Pe2Vertex anchor=b.x<=a.x?(c.x<=b.x?c:b):(c.x<a.x?c:a);
 if(area<0){Pe2Vertex t=b;b=c;c=t;area=-area;}
 /* Match native truncated gradients and leftmost-vertex half-unit bias.
  * Quarter samples extend the native integer-position attribute plane. */
 const int av[5]={a.r,a.g,a.b,a.u,a.v},bv[5]={b.r,b.g,b.b,b.u,b.v};
 const int cv[5]={c.r,c.g,c.b,c.u,c.v};
 const int origin[5]={anchor.r,anchor.g,anchor.b,anchor.u,anchor.v};
 int64_t step_x[5],step_y[5];
 for(int i=0;i<5;++i){
  step_x[i]=((int64_t)(bv[i]-av[i])*(c.y-a.y)-(int64_t)(cv[i]-av[i])*(b.y-a.y))*4096/area;
  step_y[i]=((int64_t)(cv[i]-av[i])*(b.x-a.x)-(int64_t)(bv[i]-av[i])*(c.x-a.x))*4096/area;
 }
 int minx=a.x<b.x?a.x:b.x;if(c.x<minx)minx=c.x;
 int maxx=a.x>b.x?a.x:b.x;if(c.x>maxx)maxx=c.x;
 int miny=a.y<b.y?a.y:b.y;if(c.y<miny)miny=c.y;
 int maxy=a.y>b.y?a.y:b.y;if(c.y>maxy)maxy=c.y;
 minx=(minx-f->page_x)*f->scale;maxx=(maxx-f->page_x)*f->scale;
 miny=(miny-f->page_y)*f->scale;maxy=(maxy-f->page_y)*f->scale;
 if(minx<f->sx0)minx=f->sx0;if(miny<f->sy0)miny=f->sy0;
 if(maxx>f->sx1)maxx=f->sx1;if(maxy>f->sy1)maxy=f->sy1;
 for(int y=miny;y<maxy;++y)for(int x=minx;x<maxx;++x){
  int units=2*f->scale;
  int x4=f->page_x*units+x*2+1,y4=f->page_y*units+y*2+1;
  int64_t wa=Pe2Edge(b,c,x4,y4,units),wb=Pe2Edge(c,a,x4,y4,units),wc=Pe2Edge(a,b,x4,y4,units);
  if(wa<0 || (!wa&&!Pe2Top(b,c)) || wb<0 || (!wb&&!Pe2Top(c,a)) ||
     wc<0 || (!wc&&!Pe2Top(a,b)))continue;
  int attributes[5];
  for(int i=0;i<5;++i){
   /* Rational sample positions preserve the native truncated 12-bit plane.
    * Floor then 8-bit wrap equals the original power-of-two unsigned path. */
   int64_t value=(int64_t)(origin[i]*4096+2048)*units+
       (x4-anchor.x*units)*step_x[i]+(y4-anchor.y*units)*step_y[i];
   attributes[i]=Pe2Attribute(value,units*4096);
  }
  int r=gouraud?attributes[0]:a.r,g=gouraud?attributes[1]:a.g;
  int blue=gouraud?attributes[2]:a.b;
  /* Phase is absolute native VRAM, never page-local or scaled output XY.
   * Both subpixel lanes retain the containing native pixel's matrix cell. */
  int native_x=f->page_x+x/f->scale,native_y=f->page_y+y/f->scale;
  int d=do_dither && gouraud ? Pe2Dither(native_x,native_y) : 0;
  uint16_t texel=Pe2PackDither(r,g,blue,d);
  if(sample){
   texel=sample(context,attributes[3],attributes[4],r,g,blue,native_x,native_y);
   if(!PE2_SAMPLE_VISIBLE(texel))continue;}
  if(Pe2Write(paint,&f->pixels[y*f->w+x],texel,sample!=NULL))f->drawn=true;
 }
 return true;
}
/* Axis-aligned native sprites have integer UV origins and unit texel steps.
 * Quarter/three-quarter destination samples share floor(local position): no
 * fractional source detail is supplied by this API. Horizontal grid stretching
 * is deliberately rejected by the caller rather than inventing UV phase. */
PE2_KEEP bool Pe2Rectangle(Pe2Frame *f,int ox,int oy,int w,int h,
 int u,int v,uint16_t color,Pe2Sample sample,void *ctx,Pe2Paint paint,bool unsupported){
 if(!f || !f->active || f->failed)return false;
 if(unsupported || ox < -2048 || ox>2047 || oy < -2048 || oy>2047 ||
    w>32767 || h>32767){Pe2Reject(f);return false;}
 if(w<=0 || h<=0)return true;
 int x0=(ox-f->page_x)*f->scale,y0=(oy-f->page_y)*f->scale;
 int x1=x0+w*f->scale,y1=y0+h*f->scale;
 if(x0<f->sx0)x0=f->sx0;if(y0<f->sy0)y0=f->sy0;
 if(x1>f->sx1)x1=f->sx1;if(y1>f->sy1)y1=f->sy1;
 for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
  uint16_t c=color;
  if(sample){
   int local_x4=(f->page_x-ox)*f->scale+x;
   int local_y4=(f->page_y-oy)*f->scale+y;
   c=sample(ctx,(u+local_x4/f->scale)&255,(v+local_y4/f->scale)&255,
       local_x4,local_y4,f->scale);
   ++f->material_total;
   if(!PE2_SAMPLE_VISIBLE(c))continue;
  }
  if(Pe2Write(paint,&f->pixels[y*f->w+x],c,sample!=NULL)){
   f->drawn=true;
   if(sample && PE2_MATERIAL_USED)++f->material_covered;
  }
 }
 return true;
}

PE2_KEEP const uint16_t *Pe2Output(Pe2Frame *f,int page_x,int page_y,int w,int h){
 if(!f || !f->active || f->failed || !f->drawn)return NULL;
 if(w<=0 || w>320 || h<=0 || h>240){Pe2RejectReason(f,PE2_PAGE);return NULL;}
 if(page_x!=f->page_x || page_y!=f->page_y || w*f->scale!=f->w || h*f->scale!=f->h){
  Pe2RejectReason(f,PE2_PAGE);return NULL;}
 return f->pixels;
}
#endif
