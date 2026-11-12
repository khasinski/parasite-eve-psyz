#include <psyz.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "../internal.h"
#include "rasterizer.h"
/* RGB555 to RGB888 as every presented and captured frame: the PS1 video DAC
 * spans the full range, so channel c shows as round(c * 255 / 31) (the
 * GPU's UNORM expansion and color_5to8 in sdl3_common.h). */
static unsigned char Expand5(unsigned c) { return (unsigned char)((c * 255 + 15) / 31); }
static bool pe2_sample_visible,pe2_material_used;
#define PE2_MATERIAL_USED pe2_material_used
#define PE2_SAMPLE_VISIBLE(value) (pe2_sample_visible)
#include "cpu-raster2x.h"

typedef struct {
    int x, y;
} Point;

typedef struct {
    int x, y;
    int u, v;
    int r, g, b;
    double fx; /* exact horizontal-grid fraction, presentation only */
} SwVertex;

#define SEMITRANSP 0x02
#define TEXTURED 0x04
#define EXTRA_VERTEX 0x08
#define GOURAUD 0x10
#define TRIANGLE 0x20

static u16 vram[VRAM_W * VRAM_H];
static Pe2Frame raster2x;
#ifdef PE_PS5
static PeRenderBuffer geometry_rgb;
static PeRenderBuffer wide_rgb;
#endif
void SoftwareRasterizer_ReleaseGeometryFrame(void *pixels){
#ifdef PE_PS5
 if(pixels && (pixels==geometry_rgb.data || pixels==wide_rgb.data))return;
#endif
 free(pixels);
}
void SoftwareRasterizer_Reject2x(void) { Pe2Reject(&raster2x); SoftwareRasterizer_WideReject(); }
void SoftwareRasterizer_Begin2x(int x,int y,int w,int h,int scale) {
    (void)Pe2Begin(&raster2x,scale,vram,VRAM_W,x,y,w,h);
}
void SoftwareRasterizer_FreeGeometry(void) {
    SoftwareRasterizer_WideReset();
#ifdef PE_PS5
    raster2x.active=false;raster2x.failed=true;
    bool rgb_ok=PeRenderRelease(&geometry_rgb),native_ok=PeRenderRelease(&raster2x.direct);
    /* Drop the alias immediately after successful unmap, even when physical
     * release or the independent RGB arena needs a later teardown retry. */
    raster2x.pixels=raster2x.direct.data;
    if(!raster2x.pixels)raster2x.capacity=0;
    if(rgb_ok && native_ok)memset(&raster2x,0,sizeof(raster2x));
#else
    free(raster2x.pixels);memset(&raster2x,0,sizeof(raster2x));
#endif
}
int SoftwareRasterizer_GeometryReason(void) {
    return raster2x.reason ? raster2x.reason : PE2_EMPTY;
}
unsigned char *SoftwareRasterizer_Alloc2x(int x,int y,int w,int h,int scale) {
    if(raster2x.scale!=scale){Pe2RejectReason(&raster2x,PE2_CONFIG);return NULL;}
    const u16 *src=Pe2Output(&raster2x,x,y,w,h);
    if(!src)return NULL;
    size_t count=(size_t)raster2x.w*raster2x.h;
    if(count>PE2_MAX_PIXELS){Pe2RejectReason(&raster2x,PE2_BOUNDS);return NULL;}
#ifdef PE_PS5
    unsigned char *out=PeRenderReserve(&geometry_rgb,PE2_MAX_PIXELS*3)?geometry_rgb.data:NULL;
#else
    unsigned char *out=malloc(count*3);
#endif
    if(!out){Pe2RejectReason(&raster2x,PE2_ALLOC);return NULL;}
    for(size_t i=0;i<count;++i) {
        u16 c=src[i];
        out[i*3]=Expand5(c&31);
        out[i*3+1]=Expand5((c>>5)&31);
        out[i*3+2]=Expand5((c>>10)&31);
    }
    return out;
}

/* Ordered materials borrow the immediately following primitive selection.
 * No independent shadow, heap frame, texture authority or final overpaint. */
static int (*material_sample)(unsigned,unsigned,uint16_t,uint16_t[4]);
void SoftwareRasterizer_InvalidateMaterial(void) {
    /* No detached detail exists. Direct writes already synchronize geometry;
     * atlas reset separately rejects the in-flight epoch before freeing art. */
}
void SoftwareRasterizer_SetMaterialSampler(
    int (*sample)(unsigned,unsigned,uint16_t,uint16_t[4])) {material_sample=sample;}
void SoftwareRasterizer_MaterialCoverage(unsigned long long *covered,unsigned long long *total) {
    *covered=raster2x.material_covered;*total=raster2x.material_total;
}

static bool dither_enabled;

static int SoftwareDither(void) {
    return dither_enabled && Psyz_VideoGetDitheringMode() != PSYZ_DITHER_OFF;
}
static Point draw_offset;
static Point draw_area_start;
static Point draw_area_end = {VRAM_W - 1, VRAM_H - 1};
static u16 cur_tpage;
static bool pe2_sprite_mode_unsupported;
static u8 twin_and_x = 0xff;
static u8 twin_and_y = 0xff;
static u8 twin_or_x;
static u8 twin_or_y;
static bool set_mask_bit;
static bool preserve_masked;
static unsigned grid_source = 1;
static unsigned grid_target = 1;

static int Sign11(int value) {
    value &= 0x7ff;
    return (value ^ 0x400) - 0x400;
}

static int Min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int Max3(int a, int b, int c) {
    int m = a > b ? a : b;
    return m > c ? m : c;
}

static int Clamp5(int value) { return CLAMP(value, 0, 31); }

static u16 PackColor(int r, int g, int b, bool mask) {
    return (u16)(Clamp5(r) | (Clamp5(g) << 5) | (Clamp5(b) << 10) |
                 (mask ? 0x8000 : 0));
}

/* Shared native RGB555 destination semantics; caller owns addressing. */
static bool NativeDestinationWrite(u16 *out,u16 src,bool semi,int abr,
    bool set_mask_bit,bool preserve_masked) {
    if (preserve_masked && (*out & 0x8000)) {
        return false;
    }
    if (semi) {
        u16 dst = *out;
        int sr = src & 31;
        int sg = (src >> 5) & 31;
        int sb = (src >> 10) & 31;
        int dr = dst & 31;
        int dg = (dst >> 5) & 31;
        int db = (dst >> 10) & 31;
        switch (abr & 3) {
        case 0:
            sr = (sr + dr) >> 1;
            sg = (sg + dg) >> 1;
            sb = (sb + db) >> 1;
            break;
        case 1:
            sr += dr;
            sg += dg;
            sb += db;
            break;
        case 2:
            sr = dr - sr;
            sg = dg - sg;
            sb = db - sb;
            break;
        case 3:
            sr = dr + sr / 4;
            sg = dg + sg / 4;
            sb = db + sb / 4;
            break;
        }
        src = PackColor(sr, sg, sb, (src & 0x8000) != 0);
    }
    *out =
        (u16)((src & 0x7fff) | ((src | (set_mask_bit ? 0x8000 : 0)) & 0x8000));
    return true;
}

static bool WritePixelSource(int x,int y,u16 src,bool semi,int abr,const u16 *material) {
    if (x < draw_area_start.x || x > draw_area_end.x || y < draw_area_start.y ||
        y > draw_area_end.y || x < 0 || x >= VRAM_W || y < 0 || y >= VRAM_H) {
        return false;
    }
    u16* out = &vram[y * VRAM_W + x];
    if (!NativeDestinationWrite(out,src,semi,abr,set_mask_bit,preserve_masked)) return false;
    if(raster2x.active && !raster2x.failed && raster2x.scale==1 &&
       x>=raster2x.page_x && x<raster2x.page_x+raster2x.w &&
       y>=raster2x.page_y && y<raster2x.page_y+raster2x.h) {
        u16 *destination=&raster2x.pixels[(y-raster2x.page_y)*raster2x.w+x-raster2x.page_x];
        if(NativeDestinationWrite(destination,material?*material:src,semi,abr,set_mask_bit,preserve_masked)) {
            raster2x.drawn=true;
            if(material)++raster2x.material_covered;
        }
    }
    return true;
}

static bool WritePixel(int x,int y,u16 src,bool semi,int abr) {
    return WritePixelSource(x,y,src,semi,abr,NULL);
}

/* The GPU's CLUT cache (Draw_ClearCache): the palette of the textured
 * primitive that last loaded it. */
static u16 clut_cache[256];
static u32 clut_cache_reg = 0xFFFFFFFFu;
static bool clut_cache_8bit;

static void ClutCacheUpdate(u16 tpage, u16 clut) {
    const int depth = (tpage >> 7) & 3;
    if (depth >= 2 || (clut == clut_cache_reg && (depth == 0 || clut_cache_8bit)))
        return;
    clut_cache_reg = clut;
    clut_cache_8bit = depth == 1;
    const int cx = (clut & 0x3f) * 16, cy = (clut >> 6) & 511;
    for (int i = 0; i < (depth ? 256 : 16); i++)
        clut_cache[i] = vram[cy * VRAM_W + ((cx + i) & 1023)];
}

static void Software_ClearCache(void) {
    clut_cache_reg = 0xFFFFFFFFu;
    clut_cache_8bit = false;
}

static u16 SampleTexture(int u, int v, u16 tpage, u16 clut) {
    (void)clut;
    u = ((u & twin_and_x) | twin_or_x) & 0xff;
    v = ((v & twin_and_y) | twin_or_y) & 0xff;
    int tx = (tpage & 0xf) * 64;
    int ty = ((tpage >> 4) & 1) * 256 + v;
    int depth = (tpage >> 7) & 3;
    if (depth == 0) {
        u16 packed = vram[ty * VRAM_W + ((tx + u / 4) & 1023)];
        return clut_cache[(packed >> ((u & 3) * 4)) & 15];
    }
    if (depth == 1) {
        u16 packed = vram[ty * VRAM_W + ((tx + u / 2) & 1023)];
        return clut_cache[(packed >> ((u & 1) * 8)) & 255];
    }
    return vram[ty * VRAM_W + ((tx + u) & 1023)];
}

static const int dither[4][4] = {
    {-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

static u16 ShadeTexel(
    u16 texel, int r, int g, int b, bool raw, int x, int y, bool do_dither) {
    int tr = texel & 31;
    int tg = (texel >> 5) & 31;
    int tb = (texel >> 10) & 31;
    if (raw) {
        return texel;
    }
    if (do_dither) {
        int d = dither[y & 3][x & 3];
        tr = Clamp5((CLAMP((tr * r) >> 4, 0, 255) + d) >> 3);
        tg = Clamp5((CLAMP((tg * g) >> 4, 0, 255) + d) >> 3);
        tb = Clamp5((CLAMP((tb * b) >> 4, 0, 255) + d) >> 3);
    } else {
        tr = CLAMP((tr * r) >> 7, 0, 31);
        tg = CLAMP((tg * g) >> 7, 0, 31);
        tb = CLAMP((tb * b) >> 7, 0, 31);
    }
    return PackColor(tr, tg, tb, (texel & 0x8000) != 0);
}

static int Pe2Dither(int x,int y) {return dither[y&3][x&3];}
static u16 Pe2PackDither(int r,int g,int b,int d) {
    return PackColor((r+d)>>3,(g+d)>>3,(b+d)>>3,false);
}
typedef struct {u16 tpage,clut;int r,g,b;bool raw;bool do_dither;bool material;} Pe2Texture;
static u16 Pe2NativeSample(void *ctx,int u,int v,int local_x,int local_y,int scale) {
    Pe2Texture *t=ctx;u16 texel=SampleTexture(u,v,t->tpage,t->clut);
    bool visible=texel!=0;
    pe2_material_used=false;
    /* Native zero/STP and reference equality precede any art substitution.
     * The atlas producer uses cell i per 16x16 sprite; px/py are sprite-local,
     * not texture-page UV. API supplies four samples at atlas offsets 1 and 3
     * inside each 4x4 source texel. Higher scales reuse those exact samples. */
    if(texel && t->material && material_sample && scale>0) {
        unsigned px=(unsigned)(local_x/scale),py=(unsigned)(local_y/scale);
        u16 samples[4];
        if(material_sample(px,py,texel,samples)) {
            unsigned sx=(unsigned)(((local_x%scale)*2+1)/scale);
            unsigned sy=(unsigned)(((local_y%scale)*2+1)/scale);
            texel=(u16)((samples[sy*2+sx]&0x7fff)|(texel&0x8000));
            pe2_material_used=true;
        }
    }
    /* Test source zero before modulation: nonzero texels tinted black still write. */
    pe2_sample_visible=visible;
    if(!visible)return 0;
    return ShadeTexel(texel,t->r,t->g,t->b,t->raw,0,0,false);
}
static u16 Pe2NativePolygonSample(void *ctx,int u,int v,int r,int g,int b,int x,int y) {
    Pe2Texture *t=ctx;u16 texel=SampleTexture(u,v,t->tpage,t->clut);
    /* Transparency precedes shading; a nonzero texel modulated to black writes.
     * Raw texture bypasses all vertex RGB through the native shader. */
    pe2_sample_visible=texel!=0;
    if(!texel)return 0;
    return ShadeTexel(texel,r,g,b,t->raw,x,y,t->do_dither && !t->raw);
}
/* Full possible 256x256 direct texture footprint, including X wrap.
 * Compare against every potential canonical destination, not display alone.
 * Windowing can only reduce this footprint. No snapshots or feedback from 2x. */
static bool Pe2TextureDisjoint(SwVertex a,SwVertex b,SwVertex c,u16 tpage,u16 clut) {
    int x0=CLAMP(Min3(a.x,b.x,c.x),0,1023);
    int x1=CLAMP(Max3(a.x,b.x,c.x),0,1023);
    int y0=CLAMP(Min3(a.y,b.y,c.y),0,511);
    int y1=CLAMP(Max3(a.y,b.y,c.y),0,511);
    int ty=((tpage>>4)&1)*256,tx=(tpage&15)*64;
    int depth=(tpage>>7)&3;
    if(depth==3)return false;
    /* Include all possible packed words, then the entire wrapped CLUT. */
    if(!(y1<ty || y0>ty+255)) {
        int words=256>>(2-depth);
        for(int u=0;u<words;++u){int x=(tx+u)&1023;if(x>=x0 && x<=x1)return false;}
    }
    if(depth<2) {
        int cy=(clut>>6)&511,cx=(clut&63)*16;
        if(cy>=y0 && cy<=y1)for(int i=0;i<(depth?256:16);++i) {
            int x=(cx+i)&1023;if(x>=x0 && x<=x1)return false;
        }
    }
    return true;
}
#include "cpu-widescreen.h"

/* ===== Native PS1 polygon and line rasterization =====
 *
 * The GPU scans a triangle top to bottom in two parts split at the middle
 * vertex. Both edges are 32.32 fixed-point X positions stepped once per
 * line from a start just below the vertex's integer X, and a line covers the
 * pixels [left, right) of the two truncated positions. This gives the
 * hardware's fill convention: the top and left edges are inside, the bottom
 * and right edges outside, and pixel centres are not sampled. Colours and
 * texture coordinates step by per-pixel and per-line gradients (12 integer +
 * 12 fractional bits, kept in 32-bit wrapping registers) from a half-unit
 * bias at the leftmost vertex. Lines step a 32.32 position along the major
 * axis. The arithmetic follows the Mednafen/DuckStation software GPU, which
 * reproduces hardware test results; RGB555 output, dithering, blending and
 * the mask bit are shared with sprites through ShadeTexel/WritePixel. */

typedef struct {
    bool textured, raw, semi, gouraud, dither;
    u16 tpage, clut;
    int abr;
    int r, g, b;           /* flat colour */
    u32 dx[5], dy[5];      /* R, G, B, U, V gradients */
} NativePoly;

static void NativeStep(u32* attr, const u32* step, int count) {
    for (int i = 0; i < 5; i++)
        attr[i] += step[i] * (u32)count;
}

static void NativePixel(const NativePoly* p, int x, int y, const u32* attr) {
    int r = p->gouraud ? (int)(attr[0] >> 24) : p->r;
    int g = p->gouraud ? (int)(attr[1] >> 24) : p->g;
    int b = p->gouraud ? (int)(attr[2] >> 24) : p->b;
    u16 color;
    bool blend = p->semi;
    if (p->textured) {
        u16 texel = SampleTexture((int)(attr[3] >> 24), (int)(attr[4] >> 24),
                                  p->tpage, p->clut);
        if (!texel)
            return;
        blend = p->semi && (texel & 0x8000);
        color = ShadeTexel(texel, r, g, b, p->raw, x, y, p->dither);
    } else {
        int d = p->dither ? dither[y & 3][x & 3] : 0;
        color = PackColor((r + d) >> 3, (g + d) >> 3, (b + d) >> 3, false);
    }
    WritePixel(x, y, color, blend, p->abr);
}

static void NativeSpan(const NativePoly* p, int y, int x_start, int x_bound,
                       const u32* line_attr) {
    int width = x_bound - x_start;
    int x = Sign11(x_start);
    if (x < draw_area_start.x) {
        int delta = draw_area_start.x - x;
        x_start += delta;
        x += delta;
        width -= delta;
    }
    if (x + width > draw_area_end.x + 1)
        width = draw_area_end.x + 1 - x;
    if (width <= 0)
        return;
    u32 attr[5];
    memcpy(attr, line_attr, sizeof(attr));
    NativeStep(attr, p->dx, x_start);
    do {
        NativePixel(p, x, y, attr);
        x++;
        NativeStep(attr, p->dx, 1);
    } while (--width > 0);
}

typedef struct {
    uint64_t x[2], step[2]; /* left and right edge, 32.32 */
    int y, end_y;
    bool upwards;      /* drawn from y - 1 up to end_y */
} NativePart;

static int NativeEdgeX(uint64_t fp) { return (int)(s32)(u32)(fp >> 32); }

static void NativeDrawPart(const NativePoly* p, const NativePart* part,
                           const u32* origin) {
    uint64_t left = part->x[0], right = part->x[1];
    int y = part->y;
    u32 attr[5];
    memcpy(attr, origin, sizeof(attr));
    if (part->upwards) {
        if (y <= part->end_y)
            return;
        NativeStep(attr, p->dy, y);
        do {
            y--;
            left -= part->step[0];
            right -= part->step[1];
            int line = Sign11(y);
            if (line < draw_area_start.y)
                break;
            NativeStep(attr, p->dy, -1);
            if (line > draw_area_end.y)
                continue;
            NativeSpan(p, line & (VRAM_H - 1), NativeEdgeX(left),
                       NativeEdgeX(right), attr);
        } while (y > part->end_y);
    } else {
        if (y >= part->end_y)
            return;
        NativeStep(attr, p->dy, y);
        do {
            int line = Sign11(y);
            if (line > draw_area_end.y)
                break;
            if (line >= draw_area_start.y)
                NativeSpan(p, line & (VRAM_H - 1), NativeEdgeX(left),
                           NativeEdgeX(right), attr);
            y++;
            left += part->step[0];
            right += part->step[1];
            NativeStep(attr, p->dy, 1);
        } while (y < part->end_y);
    }
}

static int64_t NativeEdgeStart(int x) {
    return (int64_t)x * 4294967296LL + (4294967296LL - 2048);
}

static int64_t NativeEdgeStep(int dx, int dy) {
    int64_t bias = dx < 0 ? -(dy - 1) : (dx > 0 ? dy - 1 : 0);
    return ((int64_t)dx * 4294967296LL + bias) / dy;
}

static int NativeAttr(const SwVertex* v, int i) {
    switch (i) {
    case 0: return v->r;
    case 1: return v->g;
    case 2: return v->b;
    case 3: return v->u;
    default: return v->v;
    }
}

static void RasterTriangleNative(
    const SwVertex* v0, const SwVertex* v1, const SwVertex* v2, bool textured,
    bool raw_texture, bool semi, bool gouraud, u16 tpage, u16 clut) {
    const SwVertex* swap;
    /* tl: one bit per position, tracking the leftmost vertex (ties keep the
     * earlier vertex) through the sort by Y */
    unsigned tl;
    if (v1->x <= v0->x)
        tl = v2->x <= v1->x ? 4 : 2;
    else
        tl = v2->x < v0->x ? 4 : 1;
    if (v2->y < v1->y) {
        swap = v2; v2 = v1; v1 = swap;
        tl = ((tl >> 1) & 2) | ((tl << 1) & 4) | (tl & 1);
    }
    if (v1->y < v0->y) {
        swap = v1; v1 = v0; v0 = swap;
        tl = ((tl >> 1) & 1) | ((tl << 1) & 2) | (tl & 4);
    }
    if (v2->y < v1->y) {
        swap = v2; v2 = v1; v1 = swap;
        tl = ((tl >> 1) & 2) | ((tl << 1) & 4) | (tl & 1);
    }
    const SwVertex* vertex[3] = {v0, v1, v2};
    tl >>= 1;
    if (v0->y == v2->y)
        return;

    int64_t base = NativeEdgeStart(v0->x);
    int64_t base_step = NativeEdgeStep(v2->x - v0->x, v2->y - v0->y);
    int64_t upper_step =
        v1->y == v0->y ? 0 : NativeEdgeStep(v1->x - v0->x, v1->y - v0->y);
    int64_t lower_step =
        v2->y == v1->y ? 0 : NativeEdgeStep(v2->x - v1->x, v2->y - v1->y);
    int vo = tl != 0 ? 1 : 0;
    int vp = tl == 2 ? 3 : 0;
    bool right_facing =
        v1->y == v0->y ? v1->x > v0->x : upper_step > base_step;
    int rfi = right_facing ? 1 : 0, ofi = !rfi;

    NativePart parts[2];
    NativePart* po = &parts[vo];
    NativePart* pp = &parts[vo ^ 1];
    po->y = vertex[0 ^ vo]->y;
    po->end_y = vertex[1 ^ vo]->y;
    pp->y = vertex[1 ^ vp]->y;
    pp->end_y = vertex[2 ^ vp]->y;
    po->x[rfi] = (uint64_t)NativeEdgeStart(vertex[0 ^ vo]->x);
    po->step[rfi] = (uint64_t)upper_step;
    po->x[ofi] = (uint64_t)(base + (int64_t)(vertex[vo]->y - vertex[0]->y) * base_step);
    po->step[ofi] = (uint64_t)base_step;
    po->upwards = vo != 0;
    pp->x[rfi] = (uint64_t)NativeEdgeStart(vertex[1 ^ vp]->x);
    pp->step[rfi] = (uint64_t)lower_step;
    pp->x[ofi] =
        (uint64_t)(base + (int64_t)(vertex[1 ^ vp]->y - vertex[0]->y) * base_step);
    pp->step[ofi] = (uint64_t)base_step;
    pp->upwards = vp != 0;

    s32 det = (v1->x - v0->x) * (v2->y - v1->y) - (v2->x - v1->x) * (v1->y - v0->y);
    if (!det)
        return;
    NativePoly p = {
        .textured = textured,
        .raw = raw_texture,
        .semi = semi,
        .gouraud = gouraud,
        .dither = SoftwareDither() && (gouraud || (textured && !raw_texture)),
        .tpage = tpage,
        .clut = clut,
        .abr = (tpage >> 5) & 3,
    };
    const SwVertex* left = vertex[tl];
    p.r = left->r;
    p.g = left->g;
    p.b = left->b;
    for (int i = 0; i < 5; i++) {
        if (i < 3 ? !gouraud : !textured)
            continue;
        s32 a0 = NativeAttr(v0, i), a1 = NativeAttr(v1, i), a2 = NativeAttr(v2, i);
        s32 per_x = (a1 - a0) * (v2->y - v1->y) - (a2 - a1) * (v1->y - v0->y);
        s32 per_y = (v1->x - v0->x) * (a2 - a1) - (v2->x - v1->x) * (a1 - a0);
        p.dx[i] = (u32)(s32)((int64_t)per_x * 4096 / det) << 12;
        p.dy[i] = (u32)(s32)((int64_t)per_y * 4096 / det) << 12;
    }
    u32 origin[5];
    for (int i = 0; i < 5; i++)
        origin[i] = (((u32)NativeAttr(left, i) << 12) + (1u << 11)) << 12;
    NativeStep(origin, p.dx, -left->x);
    NativeStep(origin, p.dy, -left->y);
    for (int i = 0; i < 2; i++)
        NativeDrawPart(&p, &parts[i], origin);
}

/* Polygons whose bounding box spans 1024 or more columns or 512 or more rows
 * are not drawn. */
static bool NativeTriangleCulled(const SwVertex* a, const SwVertex* b,
                                 const SwVertex* c) {
    return Max3(a->x, b->x, c->x) - Min3(a->x, b->x, c->x) >= VRAM_W ||
           Max3(a->y, b->y, c->y) - Min3(a->y, b->y, c->y) >= VRAM_H;
}

static void RasterLineNative(const SwVertex* p0, const SwVertex* p1, bool semi,
                             bool gouraud, u16 tpage) {
    int dx = abs(p1->x - p0->x), dy = abs(p1->y - p0->y);
    int k = dx > dy ? dx : dy;
    if (dx >= VRAM_W || dy >= VRAM_H)
        return;
    if (p0->x >= p1->x && k > 0) {
        const SwVertex* swap = p0;
        p0 = p1;
        p1 = swap;
    }
    int64_t step_x = 0, step_y = 0;
    s32 step_c[3] = {0, 0, 0};
    const int c0[3] = {p0->r, p0->g, p0->b}, c1[3] = {p1->r, p1->g, p1->b};
    if (k) {
        int64_t delta[2] = {p1->x - p0->x, p1->y - p0->y};
        int64_t step[2];
        for (int i = 0; i < 2; i++)
            step[i] = (delta[i] * 4294967296LL - (delta[i] < 0 ? k - 1 : 0) +
                       (delta[i] > 0 ? k - 1 : 0)) / k;
        step_x = step[0];
        step_y = step[1];
        if (gouraud)
            for (int i = 0; i < 3; i++)
                step_c[i] = (s32)((u32)(c1[i] - c0[i]) << 12) / k;
    }
    int64_t x = (int64_t)p0->x * 4294967296LL + (1LL << 31) - 1024;
    int64_t y = (int64_t)p0->y * 4294967296LL + (1LL << 31) - (step_y < 0 ? 1024 : 0);
    s32 c[3];
    for (int i = 0; i < 3; i++)
        c[i] = (c0[i] << 12) | (1 << 11);
    bool dithered = SoftwareDither() != 0;
    int abr = (tpage >> 5) & 3;
    for (int i = 0; i <= k; i++) {
        int px = Sign11((int)(x >> 32)), py = Sign11((int)(y >> 32));
        if (px >= draw_area_start.x && px <= draw_area_end.x &&
            py >= draw_area_start.y && py <= draw_area_end.y) {
            int r = gouraud ? (u8)(c[0] >> 12) : c0[0];
            int g = gouraud ? (u8)(c[1] >> 12) : c0[1];
            int b = gouraud ? (u8)(c[2] >> 12) : c0[2];
            int d = dithered ? dither[py & 3][px & 3] : 0;
            WritePixel(px, py & (VRAM_H - 1),
                       PackColor((r + d) >> 3, (g + d) >> 3, (b + d) >> 3, false),
                       semi, abr);
        }
        x += step_x;
        y += step_y;
        for (int j = 0; j < 3; j++)
            c[j] += step_c[j];
    }
}


static void RasterTriangle(
    SwVertex a, SwVertex b, SwVertex c, bool textured, bool raw_texture,
    bool semi, bool gouraud, u16 tpage, u16 clut, bool centered, bool owned_line) {
    if(!owned_line)WideTriangle(a,b,c,textured,raw_texture,semi,gouraud,tpage,clut,centered,owned_line);
    if(raster2x.active && !raster2x.failed && raster2x.scale>1) {
        Pe2Scissor(&raster2x,draw_area_start.x,draw_area_start.y,
                   draw_area_end.x,draw_area_end.y);
        Pe2Vertex pa={a.x,a.y,a.u,a.v,a.r,a.g,a.b};
        Pe2Vertex pb={b.x,b.y,b.u,b.v,b.r,b.g,b.b};
        Pe2Vertex pc={c.x,c.y,c.u,c.v,c.r,c.g,c.b};
        Pe2Texture source={tpage,clut,a.r,a.g,a.b,raw_texture,SoftwareDither()!=0};
        if(centered && owned_line && !textured && gouraud) {
            (void)Pe2CenteredLineTriangle(&raster2x,pa,pb,pc,SoftwareDither()!=0,
                (Pe2Paint){semi,set_mask_bit,preserve_masked,(tpage>>5)&3,NativeDestinationWrite});
        } else (void)Pe2Triangle(&raster2x,pa,pb,pc,
            gouraud,SoftwareDither()!=0,textured?Pe2NativePolygonSample:NULL,&source,
            (Pe2Paint){semi,set_mask_bit,preserve_masked,(tpage>>5)&3,NativeDestinationWrite},
            (textured && (((tpage>>7)&3)==3 || !Pe2TextureDisjoint(a,b,c,tpage,clut))) || centered);
    }
    if (owned_line)
        return; /* native lines use the GPU's line stepping (DrawLine) */
    RasterTriangleNative(&a, &b, &c, textured, raw_texture, semi, gouraud,
                         tpage, clut);
}

static SwVertex VertexXY(u32 word, u32 color) {
    SwVertex v = {0};
    v.x = Sign11(word) + draw_offset.x;
    v.y = Sign11(word >> 16) + draw_offset.y;
    if (grid_source != grid_target) {
        v.x = draw_offset.x +
              (v.x - draw_offset.x) * (int)grid_target / (int)grid_source;
    }
    v.fx = (double)Sign11(word)*(double)grid_target/(double)grid_source + draw_offset.x-v.x;
    v.r = color & 0xff;
    v.g = (color >> 8) & 0xff;
    v.b = (color >> 16) & 0xff;
    return v;
}

static void DrawLine(SwVertex a, SwVertex b, bool semi, u16 tpage) {
    WideLine(a,b,semi,tpage);
    int dx = abs(b.x - a.x);
    int dy = abs(b.y - a.y);
    if (!dx && !dy) {
        if(raster2x.active && !raster2x.failed && raster2x.scale>1) {
            Pe2Scissor(&raster2x,draw_area_start.x,draw_area_start.y,
                       draw_area_end.x,draw_area_end.y);
            /* Native single point: first endpoint RGB, no dither, each owned
             * destination lane independently observes ABR and mask policy. */
            (void)Pe2Rectangle(&raster2x,a.x,a.y,1,1,0,0,
                PackColor(a.r>>3,a.g>>3,a.b>>3,false),NULL,NULL,
                (Pe2Paint){semi,set_mask_bit,preserve_masked,(tpage>>5)&3,NativeDestinationWrite},false);
        }
        RasterLineNative(&a, &b, semi, true, tpage);
        return;
    }
    int ox, oy, ex, ey;
    if (dx >= dy) {
        ox = 0;
        oy = 1;
        ex = b.x >= a.x ? 1 : -1;
        ey = 0;
    } else {
        ox = 1;
        oy = 0;
        ex = 0;
        ey = b.y >= a.y ? 1 : -1;
    }
    SwVertex q0 = a;
    SwVertex q1 = b;
    SwVertex q2 = b;
    SwVertex q3 = a;
    q1.x += ex;
    q1.y += ey;
    q2.x += ex + ox;
    q2.y += ey + oy;
    q3.x += ox;
    q3.y += oy;
    /* The 2x lane draws the line as a pixel-wide quad; VRAM uses the GPU's
     * line stepping. */
    RasterTriangle(q0, q1, q2, false, false, semi, true, tpage, 0, true, true);
    RasterTriangle(q0, q2, q3, false, false, semi, true, tpage, 0, true, true);
    RasterLineNative(&a, &b, semi, true, tpage);
}

static void RasterSprite(
    const SwVertex* origin, int w, int source_w, int h, int u, int v,
    bool textured, bool raw_texture, bool semi, u16 clut) {
    WideSprite(origin,w,source_w,h,u,v,textured,raw_texture,semi,clut);
#ifdef PE_PS5
    if(wide_extension)return; /* extension has no canonical primitive */
#endif
    if(raster2x.active && !raster2x.failed && raster2x.scale>1) {
        Pe2Scissor(&raster2x,draw_area_start.x,draw_area_start.y,
                   draw_area_end.x,draw_area_end.y);
        SwVertex end=*origin;end.x+=w>0?w-1:0;end.y+=h>0?h-1:0;
        Pe2Texture source={cur_tpage,clut,origin->r,origin->g,origin->b,raw_texture,false,
            textured && w==16 && source_w==16 && h==16 &&
            twin_and_x==255 && twin_and_y==255 && !twin_or_x && !twin_or_y};
        bool unsupported=pe2_sprite_mode_unsupported || w!=source_w ||
            (textured && !Pe2TextureDisjoint(*origin,end,end,cur_tpage,clut));
        (void)Pe2Rectangle(&raster2x,origin->x,origin->y,w,h,u,v,
            PackColor(origin->r>>3,origin->g>>3,origin->b>>3,false),
            textured?Pe2NativeSample:NULL,&source,
            (Pe2Paint){semi,set_mask_bit,preserve_masked,(cur_tpage>>5)&3,NativeDestinationWrite},unsupported);
    }
    int abr = (cur_tpage >> 5) & 3;
    for (int py = 0; py < h; py++) {
        int y = origin->y + py;
        if (y < draw_area_start.y || y > draw_area_end.y) {
            continue;
        }
        y &= VRAM_H - 1;
        for (int px = 0; px < w; px++) {
            int x = origin->x + px;
            if (x < draw_area_start.x || x > draw_area_end.x) {
                continue;
            }
            u16 color;
            bool blend = semi;
            if (textured) {
                int sample_x = w ? px * source_w / w : 0;
                u16 texel =
                    SampleTexture(u + sample_x, v + py, cur_tpage, clut);
                if (texel == 0) {
                    continue;
                }
                blend = semi && (texel & 0x8000);
                color = ShadeTexel(texel, origin->r, origin->g, origin->b,
                                   raw_texture, x, y, false);
            } else {
                color = PackColor(
                    origin->r >> 3, origin->g >> 3, origin->b >> 3, false);
            }

            u16 material_color=0;const u16 *replacement=NULL;
            if(raster2x.active && !raster2x.failed && raster2x.scale==1 && textured) {
                ++raster2x.material_total;
                if(material_sample && w==16 && source_w==16 && h==16 &&
                   !pe2_sprite_mode_unsupported && twin_and_x==255 && twin_and_y==255 &&
                   !twin_or_x && !twin_or_y) {
                    u16 samples[4];
                    u16 basis=SampleTexture(u+px,v+py,cur_tpage,clut);
                    if(material_sample((unsigned)px,(unsigned)py,basis,samples)) {
                        /* Native pixel center selects the lower/right 2x lane. */
                        material_color=ShadeTexel((samples[3]&0x7fff)|(basis&0x8000),
                            origin->r,origin->g,origin->b,raw_texture,x,y,false);
                        replacement=&material_color;
                    }
                }
            }
            WritePixelSource(x,y,color,blend,abr,replacement);
        }
    }
}

static void Software_Reset(void) {
    SoftwareRasterizer_WideReject();
    Pe2Reject(&raster2x); SoftwareRasterizer_WideReject();
    SoftwareRasterizer_InvalidateMaterial();
    cur_tpage = 0;
    pe2_sprite_mode_unsupported=false;
    draw_offset = (Point){0, 0};
    draw_area_start = (Point){0, 0};
    draw_area_end = (Point){VRAM_W - 1, VRAM_H - 1};
    set_mask_bit = preserve_masked = false;
    Software_ClearCache();
}

static void Software_SetTexpageMode(ParamDrawTexpageMode* p) {
    u16 mode = *(u16*)p;
    cur_tpage = mode & 0x1ff;
    pe2_sprite_mode_unsupported=p->tex_flip_x || p->tex_flip_y || p->tex_y_extra_vram;
    dither_enabled = (mode & 0x200) != 0;
}

static void Software_SetTextureWindow(unsigned int mask_x, unsigned int mask_y,
                                      unsigned int off_x, unsigned int off_y) {
    mask_x &= 31;
    mask_y &= 31;
    twin_and_x = (u8) ~(mask_x * 8);
    twin_and_y = (u8) ~(mask_y * 8);
    twin_or_x = (u8)((off_x & mask_x) * 8);
    twin_or_y = (u8)((off_y & mask_y) * 8);
}

static void Software_SetAreaStart(int x, int y) {
    draw_area_start = (Point){x, y};
}

static void Software_SetAreaEnd(int x, int y) { draw_area_end = (Point){x, y}; }

static void Software_SetOffset(int x, int y) {
    draw_offset = (Point){Sign11(x), Sign11(y)};
}

static int Software_SetHorizontalGrid(
    unsigned int source_width, unsigned int target_width) {
    if (!source_width || !target_width) {
        return -1;
    }
    grid_source = source_width;
    grid_target = target_width;
    return 0;
}

static void Software_SetMask(int bit0, int bit1) {
    set_mask_bit = bit0 != 0;
    preserve_masked = bit1 != 0;
}

static void Software_ClearImage(PS1_RECT* rect, u_char r, u_char g, u_char b) {
    SoftwareRasterizer_InvalidateMaterial();
    /* GP0 02h writes bit 15 clear and ignores both mask settings; libgpu
     * passes rectangles already rounded and cut at the VRAM edges. */
    u16 color = PackColor(r >> 3, g >> 3, b >> 3, false);
    WideClear(rect,color);
    int x0 = CLAMP(rect->x, 0, VRAM_W);
    int y0 = CLAMP(rect->y, 0, VRAM_H);
    int x1 = CLAMP(rect->x + rect->w, 0, VRAM_W);
    int y1 = CLAMP(rect->y + rect->h, 0, VRAM_H);
    for (int y = y0; y < y1; y++) {
        Pe2ClearSpan(&raster2x,x0,y,x1-x0,color,false);
        for (int x = x0; x < x1; x++) {
            vram[y * VRAM_W + x] = color;
        }
    }
}

/* Writes one VRAM row of an upload or copy with the GPU's mask rules: a
 * pixel with bit 15 set is kept while "check mask" is on, and "set mask"
 * forces bit 15 of each written pixel. Rows wrap at the VRAM width. */
static void TransferRow(int x, int y, int w, const u16* src) {
    u16* row = &vram[(y & (VRAM_H - 1)) * VRAM_W];
    if (!set_mask_bit && !preserve_masked && x >= 0 && x + w <= VRAM_W) {
        memcpy(&row[x], src, (size_t)w * sizeof(u16));
    } else {
        for (int i = 0; i < w; i++) {
            u16* p = &row[(x + i) & (VRAM_W - 1)];
            if (!preserve_masked || !(*p & 0x8000))
                *p = (u16)(src[i] | (set_mask_bit ? 0x8000 : 0));
        }
    }
    if (x >= 0 && x + w <= VRAM_W)
        Pe2CanonicalSpan(&raster2x, x, y & (VRAM_H - 1), w, &row[x]);
}

static void Software_LoadImage(PS1_RECT* rect, u_long* data) {
    SoftwareRasterizer_WideReject();
    SoftwareRasterizer_InvalidateMaterial();
    if (rect->w <= 0 || rect->h <= 0) {
        return;
    }
    const u16* src = (const u16*)data;
    for (int y = 0; y < rect->h; y++)
        TransferRow(rect->x & (VRAM_W - 1), rect->y + y, rect->w,
                    &src[(size_t)y * rect->w]);
}

static void Software_StoreImage(PS1_RECT* rect, u_long* data) {
    if (rect->w <= 0 || rect->h <= 0) {
        return;
    }
    u16* dst = (u16*)data;
    if (rect->x >= 0 && rect->y >= 0 && rect->x + rect->w <= VRAM_W &&
        rect->y + rect->h <= VRAM_H) {
        for (int y = 0; y < rect->h; y++) {
            memcpy(&dst[y * rect->w], &vram[(rect->y + y) * VRAM_W + rect->x],
                   (size_t)rect->w * sizeof(u16));
        }
        return;
    }
    for (int y = 0; y < rect->h; y++) {
        for (int x = 0; x < rect->w; x++) {
            int vx = rect->x + x;
            int vy = rect->y + y;
            dst[y * rect->w + x] =
                ((unsigned)vx < VRAM_W && (unsigned)vy < VRAM_H)
                    ? vram[vy * VRAM_W + vx]
                    : 0;
        }
    }
}

static void Software_MoveImage(PS1_RECT* rect, unsigned int x, unsigned int y) {
    SoftwareRasterizer_WideReject();
    SoftwareRasterizer_InvalidateMaterial();
    int w = CLAMP(rect->w, 0, VRAM_W);
    u16 line[VRAM_W];
    /* row by row from the top, each row read before it is written */
    for (int row = 0; row < rect->h; row++) {
        const u16* src = &vram[((rect->y + row) & (VRAM_H - 1)) * VRAM_W];
        for (int i = 0; i < w; i++)
            line[i] = src[(rect->x + i) & (VRAM_W - 1)];
        TransferRow((int)x, (int)y + row, w, line);
    }
}

static int Software_PushPrim(u_long* words, int max_len) {
    if (max_len <= 0) {
        Pe2Reject(&raster2x); SoftwareRasterizer_WideReject();
        return 0;
    }
    int code = (int)(words[0] >> 24);
    /* Only native F2/F3/F4/G2/G3/G4 opcodes, plus their semi variants.
     * Validate the complete packet before any shadow or canonical line write.
     * 0x44/0x54 and raw/reserved variants remain unsupported. */
    bool supported_line=code==0x40 || code==0x42 || code==0x48 || code==0x4a ||
        code==0x4c || code==0x4e || code==0x50 || code==0x52 ||
        code==0x58 || code==0x5a || code==0x5c || code==0x5e;
    int line_length = 0, line_points = 0;
    if ((code & 0xe0) == 0x40)
        line_points = Draw_LineVertices(words, max_len, &line_length);
    if(supported_line) {
        if(line_length>max_len || line_points<2) {
            Pe2Reject(&raster2x); SoftwareRasterizer_WideReject();
        }
        if(grid_source!=grid_target)Pe2Reject(&raster2x);
    }
    /* Flat/Gouraud triangle/quad, supported lines and existing sprite packets only.
     * All unsupported commands poison the entire epoch, never a partial mix. */
    if(!supported_line && code!=0x20 && code!=0x28 && code!=0x24 && code!=0x25 && code!=0x2c && code!=0x2d &&
       code!=0x30 && code!=0x38 && code!=0x34 && code!=0x35 && code!=0x3c && code!=0x3d &&
       code!=0x60 && code!=0x68 && code!=0x70 && code!=0x78 &&
       code!=0x64 && code!=0x65 && code!=0x6c && code!=0x6d &&
       code!=0x74 && code!=0x75 && code!=0x7c && code!=0x7d)
        {Pe2Reject(&raster2x); if(!WideOpcodeSupported(code))SoftwareRasterizer_WideReject();}
    bool textured = (code & TEXTURED) != 0;
    bool gouraud = (code & GOURAUD) != 0;
    bool semi = (code & SEMITRANSP) != 0;
    bool raw_texture = textured && (code & 1);
    bool polygon = !(code & 0x40);
    bool line = (code & 0x40) && !(code & 0x20);
    int at = 1;
    if (polygon && (code & TRIANGLE)) {
        int count = (code & EXTRA_VERTEX) ? 4 : 3;
        int required = 1 + count * (1 + textured) + (count - 1) * gouraud;
        if(max_len<required){Pe2Reject(&raster2x); SoftwareRasterizer_WideReject();}
        if (max_len < required) {
            return max_len;
        }
        SwVertex v[4] = {0};
        u16 clut = 0;
        u16 tpage = cur_tpage;
        u32 color = words[0];
        for (int i = 0; i < count && at < max_len; i++) {
            v[i] = VertexXY(words[at++], color);
            if (textured && at < max_len) {
                u32 uv = words[at++];
                v[i].u = uv & 0xff;
                v[i].v = (uv >> 8) & 0xff;
                if (i == 0) {
                    clut = uv >> 16;
                } else if (i == 1) {
                    tpage = uv >> 16;
                }
            }
            if (gouraud && i + 1 < count && at < max_len) {
                color = words[at++];
            }
        }
        if (textured) {
            /* A textured polygon's page (bits 0-8) becomes the GPU's drawing
             * mode, as an E1h command would set it: later sprites use it. */
            cur_tpage = (u16)((cur_tpage & ~0x1ff) | (tpage & 0x1ff));
            ClutCacheUpdate(tpage, clut);
        }
        if (!NativeTriangleCulled(&v[0], &v[1], &v[2]))
            RasterTriangle(v[0], v[1], v[2], textured, raw_texture, semi,
                           gouraud, tpage, clut, false, false);
        if (count == 4 && !NativeTriangleCulled(&v[2], &v[1], &v[3])) {
            RasterTriangle(v[2], v[1], v[3], textured, raw_texture, semi,
                           gouraud, tpage, clut, false, false);
        }
        return at;
    }
    if (line) {
        /* two vertices, or a polyline up to its terminator */
        if (line_length > max_len) {
            Pe2Reject(&raster2x); SoftwareRasterizer_WideReject();
            return max_len;
        }
        SwVertex prev = {0};
        u32 color = words[0];
        for (int i = 0; i < line_points; i++) {
            if (gouraud && i)
                color = words[at++];
            SwVertex cur = VertexXY(words[at++], color);
            if (i) {
                DrawLine(prev, cur, semi, cur_tpage);
            }
            prev = cur;
        }
        return line_length;
    }
    if ((code & 0x60) == 0x60) {
        int required = 2 + textured + ((code & 0x18) == 0);
        if(max_len<required){Pe2Reject(&raster2x); SoftwareRasterizer_WideReject();}
        if (max_len < required) {
            return max_len;
        }
        SwVertex origin = VertexXY(words[at++], words[0]);
        /* rectangle positions wrap to 11 bits after the drawing offset */
        origin.x = Sign11(origin.x);
        origin.y = Sign11(origin.y);
        u16 clut = 0;
        int u = 0, v = 0;
        if (textured && at < max_len) {
            u32 uv = words[at++];
            u = uv & 0xff;
            v = (uv >> 8) & 0xff;
            clut = uv >> 16;
            ClutCacheUpdate(cur_tpage, clut);
        }
        int w, h;
        switch (code & ~3) {
        case 0x60:
        case 0x64:
            w = (int)(words[at] & (VRAM_W - 1));
            h = (int)((words[at++] >> 16) & (VRAM_H - 1));
            break;
        case 0x68:
        case 0x6c:
            w = h = 1;
            break;
        case 0x70:
        case 0x74:
            w = h = 8;
            break;
        default:
            w = h = 16;
            break;
        }
        int source_w = w;
        if (grid_source != grid_target) {
            w = w * (int)grid_target / (int)grid_source;
        }
        RasterSprite(
            &origin, w, source_w, h, u, v, textured, raw_texture, semi, clut);
        return at;
    }
    WARNF("unsupported GP0 primitive %02x", code);
    return 1;
}

void SoftwareRasterizer_Reset(void) {
    SoftwareRasterizer_InvalidateMaterial();
    memset(vram, 0, sizeof(vram));
    Software_Reset();
    grid_source = grid_target = 1;
    twin_and_x = twin_and_y = 0xff;
    twin_or_x = twin_or_y = 0;
    dither_enabled = false;
}

const u16* SoftwareRasterizer_GetVram(void) { return vram; }

void SoftwareRasterizer_SetVram(const u16* pixels) {
    SoftwareRasterizer_Reject2x();
    Software_ClearCache();
    memcpy(vram, pixels, sizeof(vram));
}

static void Software_FlushBuffer(void) {}
static void Software_ResetBuffer(void) {}
static int Software_ExequeSync(void) { return 0; }

const DrawBackend software_rasterizer = {
    .reset = Software_Reset,
    .texpage_mode = Software_SetTexpageMode,
    .texture_window = Software_SetTextureWindow,
    .area_start = Software_SetAreaStart,
    .area_end = Software_SetAreaEnd,
    .offset = Software_SetOffset,
    .horizontal_grid = Software_SetHorizontalGrid,
    .mask = Software_SetMask,
    .clear_cache = Software_ClearCache,
    .clear_image = Software_ClearImage,
    .load_image = Software_LoadImage,
    .store_image = Software_StoreImage,
    .move_image = Software_MoveImage,
    .reset_buffer = Software_ResetBuffer,
    .flush_buffer = Software_FlushBuffer,
    .push_prim = Software_PushPrim,
    .exeque_sync = Software_ExequeSync,
};
