/* SPDX-License-Identifier: MIT
 * CPU presentation-only wide drawing. Original protocol is supplied by GPU
 * dispatch; no replacement camera, texture authority or canonical write path.
 * Unsqueezed coordinates are algebraically equivalent to the GPU pivot:
 * x' = x - shift (centred layers use shift=0), crop about width/2.
 */
#ifndef PE_CPU_WIDESCREEN_H
#define PE_CPU_WIDESCREEN_H
#include <SDL3/SDL_stdinc.h>
#ifdef PE_PS5
#define WIDE_WIDTH 432
#define WIDE_HEIGHT 240
#define WIDE_PIXELS ((size_t)WIDE_WIDTH*WIDE_HEIGHT*64)
_Static_assert(WIDE_PIXELS*3<=24u*1024u*1024u,"wide RGB arena cap");
/* Existing normal sample+RGB arenas and wide 2*sample+RGB: <80 MiB,
 * including five 128 KiB rounding tails. SDL/atlas decoder heap is separate. */
_Static_assert(PE2_MAX_PIXELS*5+WIDE_PIXELS*7+5*0x20000<=80u*1024u*1024u,"render arena budget");
typedef struct {
 PeRenderBuffer memory;
 uint16_t *pixels;
 int x,y,nw,nh,w,h,scale;
 bool active,failed,drawn;
 unsigned long long samples,extensions;
} WidePage;
static WidePage wide_pages[2];
static struct {int slot,shift;bool centred,field;double qx,qy;} wide_context={-1};
static int (*wide_extension)(unsigned,unsigned,unsigned,unsigned,unsigned,uint16_t*);
void SoftwareRasterizer_SetExtensionSampler(int (*fn)(unsigned,unsigned,unsigned,unsigned,unsigned,uint16_t*)) {wide_extension=fn;}
void SoftwareRasterizer_WideReject(void) {
 for(int i=0;i<2;++i)if(wide_pages[i].active)wide_pages[i].failed=true;
}
void SoftwareRasterizer_WideReset(void) {
 wide_context.slot=-1;wide_extension=NULL;
 for(int i=0;i<2;++i){wide_pages[i].active=false;wide_pages[i].failed=true;
  if(PeRenderRelease(&wide_pages[i].memory))memset(&wide_pages[i],0,sizeof(WidePage));}
 (void)PeRenderRelease(&wide_rgb);
}
void SoftwareRasterizer_WideBegin(int slot,int x,int y,int w,int h,int scale) {
 if(slot<0 || slot>1)return;
 WidePage *f=&wide_pages[slot];f->active=false;f->failed=false;f->drawn=false;
 f->samples=f->extensions=0;
 /* Independent native display pages, never a 640x480*8 heap canvas. */
 if(!Pe2ScaleValid(scale) || x<0 || y<0 || w<=0 || w>320 || h<=0 || h>240 ||
    x>1024-w || y>512-h)return;
 f->x=x;f->y=y;f->nw=w;f->nh=h;f->scale=scale;
 f->w=WIDE_WIDTH*scale;f->h=h*scale;
 if(!PeRenderReserve(&f->memory,WIDE_PIXELS*2))return;
 f->pixels=f->memory.data;
 /* Black outside native columns. Seed retained native pixels without inventing
  * side art. All new foreground/background writes are then ordered together. */
 int margin=(WIDE_WIDTH-w)/2;
 for(int py=0;py<f->h;++py)for(int px=0;px<f->w;++px){
  int nx=px/scale-margin;
  f->pixels[(size_t)py*f->w+px]=nx>=0 && nx<w?vram[(y+py/scale)*VRAM_W+x+nx]:0;
 }
 f->active=true;
}
void SoftwareRasterizer_WideContext(int slot,int centred,int field,int shift,float qx,float qy) {
 wide_context.slot=slot;wide_context.centred=centred!=0;wide_context.field=field!=0;
 wide_context.shift=shift;wide_context.qx=qx;wide_context.qy=qy;
}
static WidePage *WideDrawPage(void) {
 if(wide_context.slot<0 || wide_context.slot>1)return NULL;
 WidePage *f=&wide_pages[wide_context.slot];
 if(!grid_source || !grid_target || grid_source>4096 || grid_target>4096){f->failed=true;return NULL;}
 if(!f->active || f->failed)return NULL;
 /* The context is refreshed for every primitive, not inherited across layers
  * or display pages. Offscreen texture draws have no wide destination. */
 if(draw_area_start.y<f->y || draw_area_end.y>=f->y+f->nh ||
    draw_area_start.x<f->x || draw_area_end.x>=f->x+f->nw)return NULL;
 return f;
}
static double WideDX(void){return (wide_context.field?wide_context.qx:0)-(wide_context.centred?0:wide_context.shift);}
static double WideDY(void){return wide_context.field?wide_context.qy:0;}
static double WideX(WidePage *f,double x){return (x-f->x+(WIDE_WIDTH-f->nw)*0.5+WideDX())*f->scale;}
static double WideY(WidePage *f,double y){return (y-f->y+WideDY())*f->scale;}
static void WideClip(WidePage *f,int *x0,int *y0,int *x1,int *y1) {
 int l=0,r=f->w;
 if(draw_area_start.x>f->x || draw_area_end.x<f->x+f->nw-1){
  l=(int)SDL_floor(WideX(f,draw_area_start.x));
  r=(int)SDL_ceil(WideX(f,draw_area_end.x+1));
 }
 int t=(int)SDL_floor(WideY(f,draw_area_start.y));
 int b=(int)SDL_ceil(WideY(f,draw_area_end.y+1));
 *x0=CLAMP(*x0,CLAMP(l,0,f->w),CLAMP(r,0,f->w));
 *x1=CLAMP(*x1,CLAMP(l,0,f->w),CLAMP(r,0,f->w));
 *y0=CLAMP(*y0,CLAMP(t,0,f->h),CLAMP(b,0,f->h));
 *y1=CLAMP(*y1,CLAMP(t,0,f->h),CLAMP(b,0,f->h));
}
static void WideClear(PS1_RECT *r,uint16_t color) {
 for(int i=0;i<2;++i){WidePage *f=&wide_pages[i];if(!f->active || f->failed)continue;
  int x0=CLAMP((r->x-f->x+(WIDE_WIDTH-f->nw)/2)*f->scale,0,f->w);
  int x1=CLAMP((r->x+r->w-f->x+(WIDE_WIDTH-f->nw)/2)*f->scale,0,f->w);
  if(r->x<=f->x && r->x+r->w>=f->x+f->nw){x0=0;x1=f->w;}
  int y0=CLAMP((r->y-f->y)*f->scale,0,f->h),y1=CLAMP((r->y+r->h-f->y)*f->scale,0,f->h);
  for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){uint16_t *d=&f->pixels[(size_t)y*f->w+x];
   if(!preserve_masked || !(*d&0x8000))*d=color;}
 }
}
typedef struct {double x,y;} WidePoint;
static double WideEdge(WidePoint a,WidePoint b,double x,double y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);}
static bool WideTop(WidePoint a,WidePoint b){return b.y<a.y || (b.y==a.y && b.x>a.x);}
static void WideTriangle(SwVertex a,SwVertex b,SwVertex c,bool textured,bool raw,
 bool semi,bool gouraud,u16 tpage,u16 clut,bool centered,bool owned_line) {
 WidePage *f=WideDrawPage();if(!f)return;
 if((centered && !owned_line) || (textured && !Pe2TextureDisjoint(a,b,c,tpage,clut))){f->failed=true;return;}
 /* Match the original full-screen fade/flash expansion before the pivot. */
 if(!textured && !centered){
  int lo=Min3(a.x,b.x,c.x),hi=Max3(a.x,b.x,c.x);
  if(lo<=f->x && hi>=f->x+f->nw){
   int margin=(f->nw+5)/6+abs(wide_context.shift)+(wide_context.field?3:0);
   SwVertex *v[3]={&a,&b,&c};for(int i=0;i<3;++i){
    if(v[i]->x==lo)v[i]->x-=margin;else if(v[i]->x==hi)v[i]->x+=margin;
   }
  }
 }
 WidePoint q[3]={{WideX(f,a.x+a.fx),WideY(f,a.y)},
  {WideX(f,b.x+b.fx),WideY(f,b.y)},{WideX(f,c.x+c.fx),WideY(f,c.y)}};
 double area=WideEdge(q[0],q[1],q[2].x,q[2].y);if(!area)return;
 if(area<0){SwVertex t=b;b=c;c=t;WidePoint v=q[1];q[1]=q[2];q[2]=v;area=-area;}
 int x0=(int)SDL_floor(SDL_min(q[0].x,SDL_min(q[1].x,q[2].x)));
 int x1=(int)SDL_ceil(SDL_max(q[0].x,SDL_max(q[1].x,q[2].x)));
 int y0=(int)SDL_floor(SDL_min(q[0].y,SDL_min(q[1].y,q[2].y)));
 int y1=(int)SDL_ceil(SDL_max(q[0].y,SDL_max(q[1].y,q[2].y)));
 WideClip(f,&x0,&y0,&x1,&y1);
 const int av[5]={a.r,a.g,a.b,a.u,a.v},bv[5]={b.r,b.g,b.b,b.u,b.v},cv[5]={c.r,c.g,c.b,c.u,c.v};
 SwVertex anchor=b.x<=a.x?(c.x<=b.x?c:b):(c.x<a.x?c:a);
 const int base[5]={anchor.r,anchor.g,anchor.b,anchor.u,anchor.v};
 /* Attribute planes are native source-grid fixed point, prior to horizontal
  * placement. Undo the rational grid rather than using truncated native XY. */
 double ratio=(double)grid_source/grid_target;
 double ax=draw_offset.x+(a.x+a.fx-draw_offset.x)*ratio;
 double bx=draw_offset.x+(b.x+b.fx-draw_offset.x)*ratio;
 double cx=draw_offset.x+(c.x+c.fx-draw_offset.x)*ratio;
 double anchor_x=draw_offset.x+(anchor.x+anchor.fx-draw_offset.x)*ratio;
 double na=(bx-ax)*(c.y-a.y)-(b.y-a.y)*(cx-ax);
 int64_t dx[5]={0},dy[5]={0};
 if(!centered && na)for(int i=0;i<5;++i){
  dx[i]=(int64_t)(((bv[i]-av[i])*(double)(c.y-a.y)-(cv[i]-av[i])*(double)(b.y-a.y))*4096/na);
  dy[i]=(int64_t)(((cv[i]-av[i])*(bx-ax)-(bv[i]-av[i])*(cx-ax))*4096/na);
 }
 for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
  double wa=WideEdge(q[1],q[2],x+0.5,y+0.5),wb=WideEdge(q[2],q[0],x+0.5,y+0.5),wc=WideEdge(q[0],q[1],x+0.5,y+0.5);
  if(wa<0 || (!wa&&!WideTop(q[1],q[2])) || wb<0 || (!wb&&!WideTop(q[2],q[0])) || wc<0 || (!wc&&!WideTop(q[0],q[1])))continue;
  double nx=(x+0.5)/f->scale+f->x-(WIDE_WIDTH-f->nw)*0.5-WideDX();
  double ny=(y+0.5)/f->scale+f->y-WideDY();
  double native_x=draw_offset.x+(nx-draw_offset.x)*ratio;
  int attrs[5];for(int i=0;i<5;++i)attrs[i]=centered?(int)((wa*av[i]+wb*bv[i]+wc*cv[i])/area):
   ((int64_t)SDL_floor((base[i]*4096.0+2048+(native_x-anchor_x)*dx[i]+(ny-anchor.y)*dy[i])/4096.0)&255);
  int r=gouraud?attrs[0]:a.r,g=gouraud?attrs[1]:a.g,blue=gouraud?attrs[2]:a.b;
  bool blend=semi;u16 color;
  if(textured){u16 texel=SampleTexture(attrs[3],attrs[4],tpage,clut);if(!texel)continue;
   blend=semi && (texel&0x8000);color=ShadeTexel(texel,r,g,blue,raw,(int)SDL_floor(nx),(int)SDL_floor(ny),SoftwareDither() && !raw);
  }else{int d=SoftwareDither() && gouraud?dither[((int)SDL_floor(ny))&3][((int)SDL_floor(nx))&3]:0;
   color=PackColor((r+d)>>3,(g+d)>>3,(blue+d)>>3,false);}
  if(NativeDestinationWrite(&f->pixels[(size_t)y*f->w+x],color,blend,(tpage>>5)&3,set_mask_bit,preserve_masked)){f->drawn=true;++f->samples;}
 }
}
static void WideSprite(const SwVertex *a,int w,int source_w,int h,int u,int v,
 bool textured,bool raw,bool semi,u16 clut) {
 WidePage *f=WideDrawPage();if(!f)return;
 SwVertex end=*a;end.x+=w;end.y+=h;
 if(pe2_sprite_mode_unsupported || (textured && !wide_extension && !Pe2TextureDisjoint(*a,end,end,cur_tpage,clut))){f->failed=true;return;}
 if(wide_extension && (!textured || source_w!=16 || h!=16)){f->failed=true;return;}
 double ox=WideX(f,a->x+a->fx),oy=WideY(f,a->y);
 double dw=(double)source_w*grid_target/grid_source;
 if(!textured && a->x<=f->x && a->x+w>=f->x+f->nw){ox=0;dw=(double)WIDE_WIDTH;}
 if(dw<=0 || h<=0)return;
 int x0=(int)SDL_ceil(ox-0.5),x1=(int)SDL_ceil(ox+dw*f->scale-0.5);
 int y0=(int)SDL_ceil(oy-0.5),y1=(int)SDL_ceil(oy+h*f->scale-0.5);
 WideClip(f,&x0,&y0,&x1,&y1);
 for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
  double tx=(x+0.5-ox)*source_w/(dw*f->scale),ty=(y+0.5-oy)/f->scale;
  int px=(int)SDL_floor(tx),py=(int)SDL_floor(ty);if(px<0 || px>=source_w || py<0 || py>=h)continue;
  u16 color;bool blend=semi;
  if(textured){u16 texel;
   if(wide_extension){unsigned lx=CLAMP((int)((tx-px)*f->scale),0,f->scale-1),ly=CLAMP((int)((ty-py)*f->scale),0,f->scale-1);
    if(!wide_extension(px,py,lx,ly,f->scale,&texel))continue;
    ++f->extensions;
   }else{texel=SampleTexture(u+px,v+py,cur_tpage,clut);if(!texel)continue;
    if(material_sample && source_w==16 && h==16 && twin_and_x==255 && twin_and_y==255 && !twin_or_x && !twin_or_y){
     u16 samples[4];if(material_sample(px,py,texel,samples)){
      int sx=CLAMP((int)((tx-px)*2),0,1),sy=CLAMP((int)((ty-py)*2),0,1);
      texel=(samples[sy*2+sx]&0x7fff)|(texel&0x8000);}
    }
   }
   blend=semi && (texel&0x8000);color=ShadeTexel(texel,a->r,a->g,a->b,raw,a->x+px,a->y+py,false);
  }else color=PackColor(a->r>>3,a->g>>3,a->b>>3,false);
  if(NativeDestinationWrite(&f->pixels[(size_t)y*f->w+x],color,blend,(cur_tpage>>5)&3,set_mask_bit,preserve_masked)){f->drawn=true;++f->samples;}
 }
}
static void WideLine(SwVertex a,SwVertex b,bool semi,u16 tpage) {
 WidePage *f=WideDrawPage();if(!f)return;
 double ratio=(double)grid_target/grid_source;
 /* Choose the original line's major axis before the horizontal grid. Its
  * endpoint extension and pixel thickness then follow that same grid. */
 double dx=SDL_fabs((b.x+b.fx)-(a.x+a.fx))/ratio;
 int dy=abs(b.y-a.y);
 if(dx==0 && dy==0){WideSprite(&a,1,1,1,0,0,false,false,semi,0);return;}
 int ox,oy,ex,ey;
 if(dx>=dy){ox=0;oy=1;ex=b.x+b.fx>=a.x+a.fx?1:-1;ey=0;}
 else{ox=1;oy=0;ex=0;ey=b.y>=a.y?1:-1;}
 SwVertex q0=a,q1=b,q2=b,q3=a;
 q1.x+=ex;q1.fx+=ex*(ratio-1);q1.y+=ey;
 q2.x+=ex+ox;q2.fx+=(ex+ox)*(ratio-1);q2.y+=ey+oy;
 q3.x+=ox;q3.fx+=ox*(ratio-1);q3.y+=oy;
 WideTriangle(q0,q1,q2,false,false,semi,true,tpage,0,true,true);
 WideTriangle(q0,q2,q3,false,false,semi,true,tpage,0,true,true);
}
unsigned char *SoftwareRasterizer_WideOutput(int x,int y,int w,int h,int scale,int *ow,int *oh) {
 for(int i=0;i<2;++i){WidePage *f=&wide_pages[i];
  if(!f->active || f->failed || !f->drawn || f->x!=x || f->y!=y || f->nw!=w || f->nh!=h || f->scale!=scale)continue;
  if(!PeRenderReserve(&wide_rgb,WIDE_PIXELS*3))return NULL;
  unsigned char *out=wide_rgb.data;
  for(size_t n=0;n<(size_t)f->w*f->h;++n){u16 c=f->pixels[n];
   out[n*3]=Expand5(c&31);out[n*3+1]=Expand5((c>>5)&31);out[n*3+2]=Expand5((c>>10)&31);}
  *ow=f->w;*oh=f->h;return out;
 }
 return NULL;
}
#else
void SoftwareRasterizer_WideReject(void) {}
void SoftwareRasterizer_WideReset(void) {}
#define WideTriangle(...) ((void)0)
#define WideSprite(...) ((void)0)
#define WideLine(...) ((void)0)
#define WideClear(...) ((void)0)
#endif
static bool WideOpcodeSupported(int code) {
 switch(code&~2){case 0x20:case 0x28:case 0x24:case 0x25:case 0x2c:case 0x2d:
 case 0x30:case 0x38:case 0x34:case 0x35:case 0x3c:case 0x3d:
 case 0x60:case 0x68:case 0x70:case 0x78:case 0x64:case 0x65:case 0x6c:case 0x6d:
 case 0x74:case 0x75:case 0x7c:case 0x7d:return true;default:return false;}
}
#endif
