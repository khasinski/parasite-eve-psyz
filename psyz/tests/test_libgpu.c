#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <psyz.h>
#include <kernel.h>
#include <libetc.h>
#include <libgpu.h>
#if defined(__psyz) && !defined(__PSP__)
#include <SDL3/SDL.h>
#endif

#include "ztest.h"

#include "res/4bpp.h"
#include "res/16bpp.h"
#include "res/uv4bpp.h"

#ifndef LEN
#define LEN(x) ((s32)(sizeof(x) / sizeof(*(x))))
#endif

enum {
    OT_LENGTH = 1,
    OTSIZE = 1 << OT_LENGTH,
    SCREEN_WIDTH = 256,
    SCREEN_HEIGHT = 240,
};

typedef struct DBuf {
    DRAWENV draw;
    DISPENV disp;
    OT_TYPE ot[OTSIZE];
    POLY_F4 f4[8];
    POLY_FT4 ft4[8];
    POLY_G4 g4[4];
    POLY_GT4 gt4[4];
    LINE_G2 lineg2[4];
    LINE_G3 lineg3[2];
    LINE_G4 lineg4[2];
    SPRT sprt[4];
    TILE tile[4];
    DR_MODE drmode[2];
    DR_TWIN twin[4];
} DBuf;

static DBuf db[2];
static DBuf* cdb;

static void gpu_setup(void) {
    RECT clearRect = {0, 0, 0x7FFF, 0x7FFF};
    Psyz_VideoSetDitheringMode(PSYZ_DITHER_AUTO);
    Psyz_VideoSetInternalResolution(1);
    SetDefDrawEnv(&db[0].draw, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&db[0].disp, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDrawEnv(&db[1].draw, SCREEN_WIDTH, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    SetDefDispEnv(&db[1].disp, SCREEN_WIDTH, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    db[0].draw.dtd = db[1].draw.dtd = 0; // disable dithering by default
    ResetGraph(0);
    PutDrawEnv(&db[0].draw);
    PutDispEnv(&db[0].disp);
    ClearOTagR(db[0].ot, OTSIZE);
    ClearOTagR(db[1].ot, OTSIZE);
    SetDispMask(1);
    ClearImage(&clearRect, 0, 0, 0);
    DrawSync(0);
    cdb = &db[0];
}

static void gpu_teardown(void) { ResetGraph(0); }

static const zimage_cmp* frame_cmp(int tol, float prec) {
    static zimage_cmp cmp;
#ifdef __PSP__
    // GU_COLOR_5551 is slightly brighter than PS1, account for error margin
    if (tol < 2) {
        tol = 2;
    }
#endif
#ifdef __psx__
    // real hardware is the reference, its frames must be identical
    tol = 0;
    prec = 1.0f;
#endif
    cmp = *zimage_r5g5b5_with_tol_prec(tol, prec);
    return &cmp;
}

#define ASSERT_FRAME(name, tol, prec)                                          \
    zexpect_image_eq(name, zimage_frontbuffer(), frame_cmp(tol, prec))

static void Present(const char* golden) {
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME(golden, 0, 1.0f);
}

static int LoadTim(void* data, u_short* outTpage, u_short* outClut) {
    TIM_IMAGE tim;
    if (OpenTIM((u_long*)data)) {
        return 1;
    }
    if (!ReadTIM(&tim)) {
        return 1;
    }
    LoadImage(tim.prect, tim.paddr);
    if (outTpage) {
        *outTpage = GetTPage((int)tim.mode, 0, tim.prect->x, tim.prect->y);
    }
    if (tim.caddr) {
        LoadImage(tim.crect, tim.caddr);
        if (outClut) {
            *outClut = GetClut(tim.crect->x, tim.crect->y);
        }
    }
    return 0;
}

static void SetPolyF4Img(POLY_FT4* poly, int x, int y, int w, int h, int u,
                         int v, u_short tpage, u_short clut, int semitrans) {
    SetPolyFT4(poly);
    setXYWH(poly, x, y, w, h);
    setRGB0(poly, 255, 128, 128);
    setUVWH(poly, u, v, w, h);
    setSemiTrans(poly, semitrans);
    poly->tpage = tpage;
    poly->clut = clut;
}

ZTEST_SETUP(gpu) { gpu_setup(); }
ZTEST_TEARDOWN(gpu) { gpu_teardown(); }

/* Off keeps RGB888 shading; Auto remains the PS1 RGB555 reference even
 * when DRAWENV.dtd is zero. Test actual captured GPU pixels, not shader text.
 */
ZTEST(gpu, dithering_off_preserves_color_precision) {
#ifdef PSYZ_TEST_SDL_GPU
    const char* headless = getenv("PSYZ_HEADLESS");
    if (headless && !strcmp(headless, "1"))
        zskip("requires SDL GPU");
    u16 white[256];
    for (unsigned i = 0; i < 256; ++i)
        white[i] = 0xFFFF;
    RECT texture = {512, 0, 256, 1};
    LoadImage(&texture, (u_long*)white);
    DrawSync(0);
    for (unsigned scale = 1; scale <= 2; ++scale) {
        Psyz_VideoSetInternalResolution(scale);
        for (unsigned enabled = 0; enabled < 2; ++enabled) {
            DRAWENV draw = db[0].draw;
            draw.dtd = enabled;
            PutDrawEnv(&draw);
            for (unsigned mode = 0; mode < 3; ++mode) {
                /* Off -> Auto -> Off also exercises a live setting change. */
                Psyz_VideoSetDitheringMode(
                    mode == 1 ? PSYZ_DITHER_AUTO : PSYZ_DITHER_OFF);
                POLY_G4 gradient = {0};
                setPolyG4(&gradient);
                setXYWH(&gradient, 0, 8, 256, 16);
                setRGB0(&gradient, 0, 0, 0);
                setRGB2(&gradient, 0, 0, 0);
                setRGB1(&gradient, 255, 255, 255);
                setRGB3(&gradient, 255, 255, 255);
                DrawPrim(&gradient);
                POLY_GT4 shaded = {0};
                setPolyGT4(&shaded);
                setXYWH(&shaded, 0, 32, 256, 16);
                setUVWH(&shaded, 0, 0, 255, 0);
                shaded.tpage = GetTPage(2, 0, 512, 0);
                setRGB0(&shaded, 0, 0, 0);
                setRGB2(&shaded, 0, 0, 0);
                setRGB1(&shaded, 128, 128, 128);
                setRGB3(&shaded, 128, 128, 128);
                DrawPrim(&shaded);
                RECT clear = {0, 56, 256, 24};
                ClearImage(&clear, 0, 0, 0);
                setXYWH(&shaded, 0, 60, 256, 16);
                setSemiTrans(&shaded, 1);
                DrawPrim(&shaded);
                DrawSync(0);
                int w, h;
                unsigned char* rgb = Psyz_VideoAllocRenderedFrame(&w, &h);
                zassert_ptr_ne(NULL, rgb);
                for (unsigned row = 0; row < 3; ++row) {
                    unsigned char seen[256] = {0};
                    unsigned count = 0;
                    int y = (row == 0 ? 12 : row == 1 ? 36 : 64) * scale;
                    for (int x = 0; x < 256 * (int)scale; ++x) {
                        unsigned value = rgb[(y * w + x) * 3];
                        if (!seen[value]) {
                            seen[value] = 1;
                            ++count;
                        }
                    }
                    if (mode == 1)
                        zexpect_s32_eq(1, count <= 32);
                    else
                        zexpect_s32_eq(1, count > (row == 2 ? 90 : 180));
                }
                free(rgb);
            }
        }
    }
    Psyz_VideoSetDitheringMode(PSYZ_DITHER_AUTO);
    Psyz_VideoSetInternalResolution(1);
#endif
}

ZTEST(gpu, supersampling_at_max_resolution_preserves_requested_size) {
#if defined(__psyz) && !defined(__PSP__)
    const char* headless = getenv("PSYZ_HEADLESS");
    if (headless && !strcmp(headless, "1"))
        return;
    DISPENV disp;
    SetDefDispEnv(&disp, 0, 224, 320, 224);
    disp.screen.h = 224;
    PutDispEnv(&disp);
    Psyz_VideoSetAntialiasing(1);
    Psyz_VideoSetInternalResolution(8);
    zassert_s32_eq(8, Psyz_VideoGetInternalResolution());
    RECT clear = {0, 224, 320, 224};
    ClearImage(&clear, 240, 0, 0);
    int w, h;
    unsigned char* image = Psyz_VideoAllocRenderedFrame(&w, &h);
    zassert_ptr_ne(NULL, image);
    zassert_s32_eq(2560, w);
    zassert_s32_eq(1792, h);
    for (int y = 0; y < h; y += 97)
        for (int x = 0; x < w; x += 83) {
            zassert_s32_eq(image[0], image[(y * w + x) * 3]);
            zassert_s32_eq(0, image[(y * w + x) * 3 + 1]);
            zassert_s32_eq(0, image[(y * w + x) * 3 + 2]);
        }
    free(image);
    Psyz_VideoSetAntialiasing(0);
    Psyz_VideoSetInternalResolution(1);
#endif
}

/* Widescreen squeezes drawing into the display buffer to 3/4 around its
 * centre, so the margins past the screen's edges are kept; full-width fills
 * cover them too, and drawing elsewhere in VRAM is left alone. */
ZTEST(gpu, widescreen_squeezes_display_buffer_drawing) {
#ifdef PSYZ_TEST_SDL_GPU
    const char* headless = getenv("PSYZ_HEADLESS");
    if (headless && !strcmp(headless, "1"))
        zskip("requires the SDL GPU renderer");
    DISPENV disp;
    SetDefDispEnv(&disp, 0, 0, 320, 224);
    disp.screen.h = 224;
    PutDispEnv(&disp);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 320, 224);
    draw.dtd = 0;
    PutDrawEnv(&draw);
    RECT clear = {0, 0, 320, 224}, line = {0, 100, 320, 1};
    u16 row[320];
    zassert_s32_eq(0, Psyz_VideoGetWidescreen());
    zassert_s32_eq(0, Psyz_VideoSetWidescreen(1));
    zassert_s32_eq(1, Psyz_VideoGetWidescreen());

    /* x -53..0 lands on 0.25..40 */
    ClearImage(&clear, 0, 0, 0);
    TILE margin = {0};
    setTile(&margin);
    setRGB0(&margin, 255, 0, 0);
    setXY0(&margin, -53, 90);
    setWH(&margin, 53, 20);
    DrawPrim(&margin);
    DrawSync(0);
    StoreImage(&line, (u_long*)row);
    DrawSync(0);
    zexpect_s32_eq(0x001F, row[1] & 0x7FFF);
    zexpect_s32_eq(0x001F, row[38] & 0x7FFF);
    zexpect_s32_eq(0, row[42] & 0x7FFF);
    zexpect_s32_eq(0, row[160] & 0x7FFF);

    /* a fill across the screen is widened to the margins */
    ClearImage(&clear, 0, 0, 0);
    TILE fill = {0};
    setTile(&fill);
    setRGB0(&fill, 0, 255, 0);
    setXY0(&fill, 0, 90);
    setWH(&fill, 320, 20);
    DrawPrim(&fill);
    DrawSync(0);
    StoreImage(&line, (u_long*)row);
    DrawSync(0);
    zexpect_s32_eq(0x03E0, row[0] & 0x7FFF);
    zexpect_s32_eq(0x03E0, row[319] & 0x7FFF);

    /* a drawing area outside the display buffer is not squeezed */
    DRAWENV other;
    SetDefDrawEnv(&other, 512, 0, 64, 64);
    other.dtd = 0;
    PutDrawEnv(&other);
    RECT other_clear = {512, 0, 64, 64}, other_line = {512, 4, 64, 1};
    ClearImage(&other_clear, 0, 0, 0);
    TILE plain = {0};
    setTile(&plain);
    setRGB0(&plain, 0, 0, 255);
    setXY0(&plain, 0, 0);
    setWH(&plain, 8, 8);
    DrawPrim(&plain);
    DrawSync(0);
    StoreImage(&other_line, (u_long*)row);
    DrawSync(0);
    zexpect_s32_eq(0x7C00, row[0] & 0x7FFF);
    zexpect_s32_eq(0x7C00, row[7] & 0x7FFF);
    zexpect_s32_eq(0, row[8] & 0x7FFF);

    /* at a room's right end the view slides 39 left, and the buffer holds
     * it: x -80..-72 lands on 9.25..15.25, the screen's x 0 on 69.25 */
    PutDrawEnv(&draw);
    Psyz_VideoSetWidescreenPicture(0, -96, 320);
    ClearImage(&clear, 0, 0, 0);
    TILE far = {0};
    setTile(&far);
    setRGB0(&far, 255, 0, 0);
    setXY0(&far, -80, 90);
    setWH(&far, 8, 20);
    DrawPrim(&far);
    TILE edge = {0};
    setTile(&edge);
    setRGB0(&edge, 0, 255, 0);
    setXY0(&edge, 0, 90);
    setWH(&edge, 8, 20);
    DrawPrim(&edge);
    DrawSync(0);
    StoreImage(&line, (u_long*)row);
    DrawSync(0);
    zexpect_s32_eq(0x001F, row[10] & 0x7FFF);
    zexpect_s32_eq(0x001F, row[14] & 0x7FFF);
    zexpect_s32_eq(0, row[16] & 0x7FFF);
    zexpect_s32_eq(0, row[60] & 0x7FFF);
    zexpect_s32_eq(0x03E0, row[70] & 0x7FFF);
    zexpect_s32_eq(0x03E0, row[74] & 0x7FFF);
    zexpect_s32_eq(0, row[76] & 0x7FFF);

    /* centred (a menu): the screen's x 156..164 lands on 157..163 as with
     * no shift, not on 186..192 */
    ClearImage(&clear, 0, 0, 0);
    Psyz_VideoSetWidescreenCentred(1);
    TILE menu = {0};
    setTile(&menu);
    setRGB0(&menu, 0, 255, 0);
    setXY0(&menu, 156, 90);
    setWH(&menu, 8, 20);
    DrawPrim(&menu);
    DrawSync(0);
    StoreImage(&line, (u_long*)row);
    DrawSync(0);
    zexpect_s32_eq(0x03E0, row[158] & 0x7FFF);
    zexpect_s32_eq(0x03E0, row[162] & 0x7FFF);
    zexpect_s32_eq(0, row[188] & 0x7FFF);

    /* a picture narrower than the view (-20..320) shows 4:3, unshifted:
     * the screen's x 0..8 lands on 40..46, not 47.5..53.5 */
    Psyz_VideoSetWidescreenPicture(0, -20, 320);
    ClearImage(&clear, 0, 0, 0);
    DrawPrim(&edge);
    DrawSync(0);
    StoreImage(&line, (u_long*)row);
    DrawSync(0);
    zexpect_s32_eq(0x03E0, row[41] & 0x7FFF);
    zexpect_s32_eq(0x03E0, row[45] & 0x7FFF);
    zexpect_s32_eq(0, row[50] & 0x7FFF);
    Psyz_VideoSetWidescreenPicture(0, 0, 0);

    /* two touching sprites whose joint lands on a quarter pixel: x 19..35
     * on 54.25..66.25, 35..51 on 66.25..78.25. The pixels the joint cuts
     * read their own sprite's first texel, not the VRAM column before it */
    u16 texels[48 * 20];
    RECT page = {640, 0, 48, 20};
    for (int i = 0; i < LEN(texels); ++i) {
        int u = i % 48;
        texels[i] = u < 8 ? 0x7FFF : u < 24 ? 0x001F : u < 32 ? 0x7C00 : 0x03E0;
    }
    LoadImage(&page, (u_long*)texels);
    DRAWENV textured = draw;
    textured.tpage = getTPage(2, 0, 640, 0);
    PutDrawEnv(&textured);
    ClearImage(&clear, 0, 0, 0);
    SPRT left = {0};
    setSprt(&left);
    setRGB0(&left, 128, 128, 128);
    setXY0(&left, 19, 90);
    setUV0(&left, 8, 0);
    setWH(&left, 16, 20);
    DrawPrim(&left);
    SPRT right = {0};
    setSprt(&right);
    setRGB0(&right, 128, 128, 128);
    setXY0(&right, 35, 90);
    setUV0(&right, 32, 0);
    setWH(&right, 16, 20);
    DrawPrim(&right);
    DrawSync(0);
    StoreImage(&line, (u_long*)row);
    DrawSync(0);
    zexpect_s32_eq(0, row[53] & 0x7FFF);
    zexpect_s32_eq(0x001F, row[54] & 0x7FFF);
    zexpect_s32_eq(0x001F, row[65] & 0x7FFF);
    zexpect_s32_eq(0x03E0, row[66] & 0x7FFF);
    zexpect_s32_eq(0x03E0, row[77] & 0x7FFF);
    zexpect_s32_eq(0, row[78] & 0x7FFF);

    /* off again: the margin is clipped as on the PlayStation */
    zassert_s32_eq(0, Psyz_VideoSetWidescreen(0));
    zassert_s32_eq(0, Psyz_VideoGetWidescreen());
    PutDrawEnv(&draw);
    ClearImage(&clear, 0, 0, 0);
    DrawPrim(&margin);
    DrawSync(0);
    StoreImage(&line, (u_long*)row);
    DrawSync(0);
    zassert_s32_eq(0, row[1] & 0x7FFF);
#else
    zskip("requires the SDL GPU renderer");
#endif
}

ZTEST(gpu, vram_copy_follows_uploads_fills_and_copies) {
#if defined(__psyz) && !defined(__PSP__)
    const unsigned char* known = NULL;
    const unsigned short* vram;
    u16 pixels[4 * 2] = {0x1234, 0x0000, 0x8000, 0x7FFF, 1, 2, 3, 4};
    RECT load = {600, 300, 4, 2}, fill = {700, 300, 8, 4};
    zassert_ptr_eq(NULL, Psyz_GpuGetVramCopy(NULL));
    zassert_s32_eq(0, Psyz_GpuSetVramCopy(1));
    vram = Psyz_GpuGetVramCopy(&known);
    zassert_ptr_ne(NULL, vram);
    zassert_ptr_ne(NULL, known);

    /* uploads land as they are, transparent texels included */
    LoadImage(&load, (u_long*)pixels);
    DrawSync(0);
    zexpect_s32_eq(0x1234, vram[300 * 1024 + 600]);
    zexpect_s32_eq(0x0000, vram[300 * 1024 + 601]);
    zexpect_s32_eq(0x8000, vram[300 * 1024 + 602]);
    zexpect_s32_eq(4, vram[301 * 1024 + 603]);
    zexpect_u8_eq(1, known[300 * 1024 + 601]);
    zexpect_u8_eq(1, known[301 * 1024 + 603]);
    zexpect_u8_eq(0, known[300 * 1024 + 604]);

    /* fills are kept as RGB5551 */
    ClearImage(&fill, 255, 0, 8);
    DrawSync(0);
    zexpect_s32_eq(0x041F, vram[302 * 1024 + 703]);
    zexpect_u8_eq(1, known[303 * 1024 + 707]);

    /* copies move texels and what is known */
    MoveImage(&load, 640, 310);
    DrawSync(0);
    zexpect_s32_eq(0x1234, vram[310 * 1024 + 640]);
    zexpect_s32_eq(4, vram[311 * 1024 + 643]);
    zexpect_u8_eq(1, known[311 * 1024 + 643]);

    /* drawing into an area makes it unknown, other areas stay known */
    DRAWENV draw;
    SetDefDrawEnv(&draw, 696, 296, 16, 16);
    draw.dtd = 0;
    PutDrawEnv(&draw);
    TILE tile = {0};
    setTile(&tile);
    setRGB0(&tile, 0, 255, 0);
    setXY0(&tile, 0, 0);
    setWH(&tile, 2, 2);
    DrawPrim(&tile);
    DrawSync(0);
    zexpect_u8_eq(0, known[302 * 1024 + 703]);
    zexpect_u8_eq(1, known[300 * 1024 + 600]);

    /* A later upload in the same drawing area is known until the next
     * primitive. No new drawing-area command is required. */
    LoadImage(&fill, (u_long*)((u16[32]){0x1234}));
    DrawSync(0);
    zexpect_u8_eq(1, known[300 * 1024 + 700]);
    DrawPrim(&tile);
    DrawSync(0);
    zexpect_u8_eq(0, known[300 * 1024 + 700]);

    zassert_s32_eq(0, Psyz_GpuSetVramCopy(0));
    zexpect_ptr_eq(NULL, Psyz_GpuGetVramCopy(NULL));
#endif
}

#if defined(__psyz) && !defined(__PSP__)
static int TestSpriteAtlasCommand(const u_long *words,int count,void *unused) {
    (void)unused;if(count<3)return -1;
    Psyz_GpuSelectSpriteAtlas((uint64_t)(u32)words[1]|((uint64_t)(u32)words[2]<<32),(u32)words[0]&0xffffff);
    return 3;
}
#endif

ZTEST(gpu, rgb24_hd_backdrop_preserves_native_ui_and_vram) {
#if defined(__psyz) && !defined(__PSP__)
    const char *backend=getenv("PSYZ_HEADLESS");if(!backend || strcmp(backend,"gpu"))return;
    unsigned char *reference=malloc(320*240*3),*native=malloc(320*240*3);
    zassert_ptr_ne(NULL,reference);zassert_ptr_ne(NULL,native);
    for(unsigned i=0;i<320*240;++i){reference[i*3]=10;reference[i*3+1]=20;reference[i*3+2]=30;}
    memcpy(native,reference,320*240*3);native[(120*320+160)*3]=200;native[(120*320+160)*3+1]=100;native[(120*320+160)*3+2]=50;
    SDL_Surface *image=SDL_CreateSurface(1024,576,SDL_PIXELFORMAT_RGBA32);zassert_ptr_ne(NULL,image);
    SDL_FillSurfaceRect(image,NULL,SDL_MapSurfaceRGB(image,70,90,120));
    char path[512];snprintf(path,sizeof(path),"%spsyz-title-%llu.png",SDL_GetBasePath(),(unsigned long long)SDL_GetPerformanceCounter());
    zassert_s32_eq(1,SDL_SavePNG(image,path));SDL_DestroySurface(image);
    DISPENV display;SetDefDispEnv(&display,0,0,320,240);display.isrgb24=1;PutDispEnv(&display);SetDispMask(1);
    RECT upload={0,0,480,240};LoadImage(&upload,(u_long *)native);DrawSync(0);
    int w,h;unsigned char *original=Psyz_VideoAllocCapturedFrame(&w,&h);zassert_ptr_ne(NULL,original);
    zassert_s32_eq(320,w);zassert_s32_eq(240,h);
    zassert_s32_eq(1,Psyz_VideoSetRgb24Backdrop(path,reference,320,240,0,0));
    zassert_s32_eq(-1,Psyz_VideoSetRgb24Backdrop(path,reference,320,240,0x7fffffff,0));
    zassert_s32_eq(-1,Psyz_VideoSetRgb24Backdrop(path,reference,320,240,0,-1));
    SDL_RemovePath(path);
    for(int wide=0;wide<2;++wide){
        Psyz_VideoSetWidescreen(wide);unsigned char *pixels=Psyz_VideoAllocRenderedFrame(&w,&h);zassert_ptr_ne(NULL,pixels);
        zassert_s32_eq(480,h);zassert_s32_eq(wide?854:640,w);
        const unsigned char background[3]={70,90,120},ui[3]={200,100,50};
        zassert_s32_eq(0,memcmp(pixels,background,3));
        unsigned x=(w-640)/2+320;zassert_s32_eq(0,memcmp(pixels+(240*w+x)*3,ui,3));free(pixels);
        unsigned char *after=Psyz_VideoAllocCapturedFrame(&w,&h);zassert_ptr_ne(NULL,after);
        zassert_s32_eq(320,w);zassert_s32_eq(240,h);zassert_s32_eq(0,memcmp(original,after,320*240*3));free(after);
    }
    Psyz_VideoSetWidescreen(0);Psyz_VideoSetRgb24Backdrop(NULL,NULL,0,0,0,0);
    unsigned char *restored=Psyz_VideoAllocRenderedFrame(&w,&h);zassert_ptr_ne(NULL,restored);
    zassert_s32_eq(320,w);zassert_s32_eq(240,h);zassert_s32_eq(0,memcmp(original,restored,320*240*3));
    free(restored);free(original);free(reference);free(native);
#endif
}

ZTEST(gpu, widescreen_hd_extension_is_display_only) {
#if defined(__psyz) && !defined(__PSP__)
    const char *backend=getenv("PSYZ_HEADLESS");if(!backend || strcmp(backend,"gpu"))return;
    SDL_Surface *image=SDL_CreateSurface(68,136,SDL_PIXELFORMAT_RGBA32);zassert_ptr_ne(NULL,image);
    SDL_FillSurfaceRect(image,NULL,SDL_MapSurfaceRGB(image,240,80,40));
    SDL_Rect reference_half={0,68,68,68};
    SDL_FillSurfaceRect(image,&reference_half,SDL_MapSurfaceRGBA(image,0,0,0,0));
    char path[512];snprintf(path,sizeof(path),"%spsyz-wide-%llu.png",SDL_GetBasePath(),(unsigned long long)SDL_GetPerformanceCounter());
    zassert_s32_eq(1,SDL_SavePNG(image,path));SDL_DestroySurface(image);
    const uint64_t key=UINT64_C(0x89abcdef01234567);
    zassert_s32_eq(1,Psyz_GpuLoadSpriteAtlas(key,path));SDL_RemovePath(path);
    DISPENV disp;SetDefDispEnv(&disp,0,0,320,224);disp.screen.h=224;PutDispEnv(&disp);
    DRAWENV draw;SetDefDrawEnv(&draw,0,0,320,224);draw.dtd=0;PutDrawEnv(&draw);
    Psyz_VideoSetInternalResolution(2);
    RECT area={0,0,320,224};u16 *baseline=calloc(320*224,sizeof(u16)),*native=calloc(320*224,sizeof(u16));
    zassert_ptr_ne(NULL,baseline);zassert_ptr_ne(NULL,native);
    zassert_s32_eq(0,Psyz_GpuRegisterCommandHandler(0xF7,TestSpriteAtlasCommand,NULL));
    for(int wide=0;wide<2;++wide) {
        Psyz_VideoSetWidescreen(wide);Psyz_VideoSetWidescreenPicture(0,-112,432);
        ClearImage(&area,16,24,32);DrawSync(0);StoreImage(&area,(u_long *)baseline);DrawSync(0);
        int w,h;u8 *before=Psyz_VideoAllocRenderedFrame(&w,&h);zassert_ptr_ne(NULL,before);
        struct {u_long address,length;u32 words[7];} packet={0};
        packet.length=7;packet.words[0]=0xF7800000;packet.words[1]=(u32)key;packet.words[2]=(u32)(key>>32);
        packet.words[3]=PSYZ_MERGED_PRIM_NOP;packet.words[4]=0x7c808080;packet.words[5]=16u|(16u<<16);packet.words[6]=0;
        termPrim(&packet);DrawOTag((OT_TYPE *)&packet);
        packet.words[5]=(u16)-32|(16u<<16);DrawOTag((OT_TYPE *)&packet);DrawSync(0);
        u8 *after=Psyz_VideoAllocRenderedFrame(&w,&h);zassert_ptr_ne(NULL,after);
        StoreImage(&area,(u_long *)native);DrawSync(0);
        zassert_s32_eq(0,memcmp(baseline,native,320*224*sizeof(u16)));
        unsigned changed=0;for(int n=0;n<w*h*3;++n)if(before[n]!=after[n])++changed;
        zassert_s32_eq(wide,changed>0);
        if(wide)zassert_s32_eq(1,memcmp(before+(40*w+20)*3,after+(40*w+20)*3,3)!=0);
        free(before);free(after);
    }
    free(baseline);free(native);Psyz_GpuResetSpriteAtlases();Psyz_GpuRegisterCommandHandler(0xF7,NULL,NULL);
    Psyz_VideoSetWidescreen(0);Psyz_VideoSetWidescreenPicture(0,0,0);Psyz_VideoSetInternalResolution(1);
#endif
}

ZTEST(gpu, queued_hd_sprite_preserves_native_vram_coverage_and_palette_fallback) {
#if defined(__psyz) && !defined(__PSP__)
    const char *backend=getenv("PSYZ_HEADLESS");
    if(!backend || strcmp(backend,"gpu"))return;
    SDL_Surface *bmp=SDL_CreateSurface(136,272,SDL_PIXELFORMAT_RGBA32);
    zassert_ptr_ne(NULL,bmp);
    for(int y=0;y<272;++y)for(int x=0;x<136;++x){
        u8 *p=(u8 *)bmp->pixels+y*bmp->pitch+x*4;
        unsigned local_x=(unsigned)(x%68<2?0:x%68>65?63:x%68-2)/4;
        unsigned local_y=(unsigned)(y%68<2?0:y%68>65?63:y%68-2)/4;
        p[0]=y<136?240:0;p[1]=y<136?40:(u8)(((local_x+local_y)&1)?255:123);p[2]=y<136?(u8)(x%68*3):0;p[3]=255;
    }
    char path[512];snprintf(path,sizeof(path),"%spsyz-hd-%llu.bmp",SDL_GetBasePath(),(unsigned long long)SDL_GetPerformanceCounter());
    zassert_s32_eq(1,SDL_SaveBMP(bmp,path));
    char png_path[512];snprintf(png_path,sizeof(png_path),"%spsyz-hd-%llu.png",SDL_GetBasePath(),(unsigned long long)SDL_GetPerformanceCounter());
    zassert_s32_eq(1,SDL_SavePNG(bmp,png_path));SDL_DestroySurface(bmp);
    const uint64_t key=UINT64_C(0x0123456789abcdef);
    zassert_s32_eq(1,Psyz_GpuLoadSpriteAtlas(key,png_path));SDL_RemovePath(png_path);
    zassert_s32_eq(1,Psyz_GpuLoadSpriteAtlas(key+1,path));SDL_RemovePath(path);
    zassert_s32_eq(1,Psyz_GpuLoadSpriteAtlas(key,path)); // cached identity survives missing file
    zassert_s32_eq(0,Psyz_GpuLoadSpriteAtlas(key+2,path));
    zassert_s32_eq(0,Psyz_GpuRegisterCommandHandler(0xF7,TestSpriteAtlasCommand,NULL));
    DISPENV disp;SetDefDispEnv(&disp,0,0,320,224);disp.screen.h=224;PutDispEnv(&disp);
    DRAWENV draw;SetDefDrawEnv(&draw,0,0,320,224);draw.dtd=0;PutDrawEnv(&draw);
    Psyz_VideoSetInternalResolution(2);
    u8 *images[5]={0};u16 native[5][64*64];int w,h;
    for(int mode=0;mode<5;++mode){
        u16 pixels[16*16];for(unsigned i=0;i<256;++i)pixels[i]=(mode==2||mode==3)?0x7c00:((((i%16)+(i/16))&1)?0x03e0:0x01e0);
        pixels[8*16+8]=0; // the original coverage must survive a bright HD texel
        RECT texture={512,0,16,16};LoadImage(&texture,(u_long *)pixels);
        RECT clear={0,0,64,64};ClearImage(&clear,16,24,32);
        DR_TPAGE page;SetDrawTPage(&page,0,0,GetTPage(2,0,512,0));DrawPrim(&page);
        struct {u_long address,length;u32 words[7];} packet={0};
        uint64_t selected_key=mode==4?key+1:key;
        packet.length=7;packet.words[0]=(mode==1||mode==2||mode==4)?0xF7000003:0;
        packet.words[1]=(mode==1||mode==2||mode==4)?(u32)selected_key:0;
        packet.words[2]=(mode==1||mode==2||mode==4)?(u32)(selected_key>>32):0;
        packet.words[3]=PSYZ_MERGED_PRIM_NOP;packet.words[4]=0x7c808080;
        packet.words[5]=16u|(16u<<16);packet.words[6]=0;
        termPrim(&packet);DrawOTag((OT_TYPE *)&packet);
        extern void Psyz_PgxpNextFrame(void);Psyz_PgxpNextFrame();DrawSync(0);
        TILE front={0};setTile(&front);setRGB0(&front,240,240,240);setXY0(&front,20,20);setWH(&front,4,4);DrawPrim(&front);
        // A later ordinary sprite must not inherit the HD material.
        SPRT_16 ordinary={0};setSprt16(&ordinary);setRGB0(&ordinary,128,128,128);setXY0(&ordinary,40,16);setUV0(&ordinary,0,0);DrawPrim(&ordinary);
        images[mode]=Psyz_VideoAllocRenderedFrame(&w,&h);zassert_ptr_ne(NULL,images[mode]);
        StoreImage(&clear,(u_long *)native[mode]);DrawSync(0);
    }
    zassert_s32_eq(0,memcmp(native[0],native[1],sizeof(native[0])));
    zassert_s32_eq(0,memcmp(native[2],native[3],sizeof(native[0])));
    zassert_s32_eq(0,memcmp(images[2],images[3],(size_t)w*h*3)); // changed palette falls back exactly
    zassert_s32_eq(0,memcmp(images[1],images[4],(size_t)w*h*3)); // PNG and BMP render identically
    zassert_s32_eq(0,memcmp(native[1],native[4],sizeof(native[0])));
    unsigned changed=0;
    for(int y=0;y<128;++y)for(int x=0;x<128;++x){
        const u8 *a=images[0]+(y*w+x)*3,*b=images[1]+(y*w+x)*3;
        if(memcmp(a,b,3))++changed;
        if(x<32 || x>=64 || y<32 || y>=64 || (x>=40&&x<48&&y>=40&&y<48) || (x>=48&&x<50&&y>=48&&y<50))
            zassert_s32_eq(0,memcmp(a,b,3));
    }
    zassert_s32_eq(1,changed>500);
    /* A different room must be able to reuse the bounded cache. Resetting it
     * must drop a cached identity even when its original BMP is now absent,
     * while leaving the native VRAM untouched. */
    Psyz_GpuResetSpriteAtlases();
    zassert_s32_eq(0,Psyz_GpuLoadSpriteAtlas(key,path));
    {
        RECT clear={0,0,64,64};
        u16 after_reset[64*64];
        StoreImage(&clear,(u_long *)after_reset);DrawSync(0);
        zassert_s32_eq(0,memcmp(native[4],after_reset,sizeof(after_reset)));
    }
    for(int i=0;i<5;++i)free(images[i]);
    Psyz_GpuRegisterCommandHandler(0xF7,NULL,NULL);Psyz_GpuSelectSpriteAtlas(0,0);Psyz_VideoSetInternalResolution(1);
#endif
}

ZTEST(gpu, optional_model_filter_and_ssaa_preserve_native_vram_and_text) {
#ifdef PSYZ_TEST_SDL_GPU
    const char* headless = getenv("PSYZ_HEADLESS");
    if (headless && !strcmp(headless, "1"))
        return;
    extern void Psyz_PgxpNextFrame(void);
    extern void Psyz_GteDataWrite(unsigned int, unsigned int);
    extern void Psyz_GteCtrlWrite(unsigned int, unsigned int);
    extern void Psyz_GteCommand(unsigned int);
    extern void Psyz_GteStoreSxyTo(unsigned int, void*);
    u16 texture[16 * 16], baseline_vram[320 * 224], native[320 * 224];
    for (int i = 0; i < 16 * 16; ++i)
        texture[i] = (i & 1) ? 0x001f : 0x03e0;
    RECT atlas = {512, 0, 16, 16};
    LoadImage(&atlas, (u_long*)texture);
    DISPENV disp;
    SetDefDispEnv(&disp, 0, 0, 320, 224);
    disp.screen.h = 224;
    PutDispEnv(&disp);
    DRAWENV draw;
    SetDefDrawEnv(&draw, 0, 0, 320, 224);
    draw.dtd = 0;
    PutDrawEnv(&draw);
    Psyz_VideoSetGeometryCorrection(0);
    Psyz_VideoSetInternalResolution(2);
    unsigned char* images[5] = {0};
    int width = 0, height = 0;
    for (int mode = 0; mode < 5; ++mode) {
        Psyz_VideoSetModelFiltering(mode == 1 || mode == 2 || mode == 4);
        Psyz_VideoSetAntialiasing(mode == 2 || mode == 3);
        zassert_s32_eq(2, Psyz_VideoGetInternalResolution());
        Psyz_PgxpNextFrame();
        RECT clear = {0, 0, 320, 224};
        ClearImage(&clear, 0, 0, 0);
        POLY_FT3 model = {0};
        setPolyFT3(&model);
        setRGB0(&model, 128, 128, 128);
        setXY3(&model, 16, 16, 48, 21, 20, 48);
        setUV3(&model, 0, 0, 15, 0, 0, 15);
        model.tpage = GetTPage(2, 0, 512, 0);
        Psyz_GteCtrlWrite(0, 4096);
        Psyz_GteCtrlWrite(1, 0);
        Psyz_GteCtrlWrite(2, 4096);
        Psyz_GteCtrlWrite(3, 0);
        Psyz_GteCtrlWrite(4, 4096);
        Psyz_GteCtrlWrite(5, 0);
        Psyz_GteCtrlWrite(6, 0);
        Psyz_GteCtrlWrite(7, 256);
        Psyz_GteCtrlWrite(24, 0);
        Psyz_GteCtrlWrite(25, 0);
        Psyz_GteCtrlWrite(26, 256);
        const short xy[3][2] = {{16, 16}, {48, 21}, {20, 48}};
        void* dest[3] = {&model.x0, &model.x1, &model.x2};
        for (int i = 0; i < 3; ++i) {
            Psyz_GteDataWrite(0, (u16)xy[i][0] | ((u32)(u16)xy[i][1] << 16));
            Psyz_GteDataWrite(1, 0);
            Psyz_GteCommand(0x180001);
            Psyz_GteStoreSxyTo(14, dest[i]);
        }
        if (mode == 1) {
            extern int Psyz_PgxpLookupVertex(
                const void*, unsigned int, float*, float*, float*);
            float fx, fy, fw;
            unsigned xyword;
            memcpy(&xyword, &model.x0, 4);
            zassert_s32_eq(
                1, Psyz_PgxpLookupVertex(&model.x0, xyword, &fx, &fy, &fw));
        }
        if (mode == 4) {
            termPrim(&model);
            DrawOTag((OT_TYPE*)&model);
            Psyz_PgxpNextFrame();
            DrawSync(0);
        } else
            DrawPrim(&model);
        DR_TPAGE page;
        SetDrawTPage(&page, 0, 0, model.tpage);
        DrawPrim(&page);
        SPRT text = {0};
        setSprt(&text);
        setRGB0(&text, 128, 128, 128);
        setXY0(&text, 48, 0);
        setUV0(&text, 0, 0);
        setWH(&text, 16, 16);
        DrawPrim(&text);
        images[mode] = Psyz_VideoAllocRenderedFrame(&width, &height);
        zassert_ptr_ne(NULL, images[mode]);
        zassert_s32_eq(640, width);
        zassert_s32_eq(448, height);
        StoreImage(&clear, (u_long*)native);
        DrawSync(0);
        if (!mode)
            memcpy(baseline_vram, native, sizeof(native));
        else
            zassert_s32_eq(0, memcmp(baseline_vram, native, sizeof(native)));
        for (int y = 0; y < 32; ++y)
            for (int x = 96; x < 128; ++x)
                zassert_s32_eq(
                    0, memcmp(images[0] + (y * width + x) * 3,
                              images[mode] + (y * width + x) * 3, 3));
    }
    unsigned filtered = 0, antialiased = 0;
    for (int i = 0; i < width * height * 3; ++i) {
        filtered += images[0][i] != images[1][i];
        antialiased += images[1][i] != images[2][i];
    }
    printf("Optional graphics: %u filtered and %u antialiased channels; native "
           "VRAM and 2D text unchanged\n",
           filtered, antialiased);
    zassert_s32_eq(1, filtered > 0);
    zassert_s32_eq(1, antialiased > 0);
    zassert_s32_eq(0, memcmp(images[1], images[4], (size_t)width * height * 3));
    for (int i = 0; i < 5; ++i)
        free(images[i]);
    Psyz_VideoSetAntialiasing(0);
    Psyz_VideoSetModelFiltering(0);
    Psyz_VideoSetInternalResolution(1);
#else
    zskip("requires the SDL GPU renderer");
#endif
}

/* A game may build its OT, wait for two vblanks, then submit that OT. */
ZTEST(gpu, geometry_metadata_survives_present_before_submission) {
#ifdef PSYZ_TEST_SDL_GPU
    extern void Psyz_PgxpNextFrame(void);
    extern int Psyz_PgxpLookupVertex(
        const void*, unsigned int, float*, float*, float*);
    extern void Psyz_GteDataWrite(unsigned int, unsigned int);
    extern void Psyz_GteCtrlWrite(unsigned int, unsigned int);
    extern void Psyz_GteCommand(unsigned int);
    extern void Psyz_GteStoreSxyTo(unsigned int, void*);
    const char* headless = getenv("PSYZ_HEADLESS");
    if (headless && !strcmp(headless, "1"))
        return;
    Psyz_VideoSetInternalResolution(2);
    Psyz_VideoSetGeometryCorrection(1);
    Psyz_PgxpNextFrame();
    Psyz_GteCtrlWrite(0, 4096);
    Psyz_GteCtrlWrite(1, 0);
    Psyz_GteCtrlWrite(2, 4096);
    Psyz_GteCtrlWrite(3, 0);
    Psyz_GteCtrlWrite(4, 4096);
    Psyz_GteCtrlWrite(5, 0);
    Psyz_GteCtrlWrite(6, 0);
    Psyz_GteCtrlWrite(7, 512);
    Psyz_GteCtrlWrite(24, 160u << 16);
    Psyz_GteCtrlWrite(25, 112u << 16);
    Psyz_GteCtrlWrite(26, 256);
    Psyz_GteDataWrite(0, 3u | (5u << 16));
    Psyz_GteDataWrite(1, 0);
    Psyz_GteCommand(0x180001);
    unsigned int xy;
    float x, y, w;
    Psyz_GteStoreSxyTo(14, &xy);
    zassert_s32_eq(1, Psyz_PgxpLookupVertex(&xy, xy, &x, &y, &w));
    VSync(0);
    VSync(0);
    zassert_s32_eq(1, Psyz_PgxpLookupVertex(&xy, xy, &x, &y, &w));
    unsigned int recycled = xy, plain = xy;
    extern void Psyz_GteCopyPgxpSxy(const void*, const void*, unsigned int);
    Psyz_GteCopyPgxpSxy(&xy, &recycled, xy);
    zassert_s32_eq(1, Psyz_PgxpLookupVertex(&recycled, recycled, &x, &y, &w));
    Psyz_GteCopyPgxpSxy(&plain, &recycled, plain);
    zassert_s32_eq(0, Psyz_PgxpLookupVertex(&recycled, recycled, &x, &y, &w));
    Psyz_PgxpNextFrame();
    zassert_s32_eq(0, Psyz_PgxpLookupVertex(&xy, xy, &x, &y, &w));
    Psyz_VideoSetGeometryCorrection(0);
    Psyz_VideoSetInternalResolution(1);
#else
    zskip("requires the SDL GPU renderer");
#endif
}

/* Keep the raw subrectangle blit as a diagnostic: its rounding varies by
 * backend. The origin-zero presentation used by the renderer must be stable. */
ZTEST(gpu, presentation_crop_is_stable_across_field_buffers) {
#if defined(__psyz) && !defined(__PSP__)
    SDL_GPUDevice* dev = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV |
            SDL_GPU_SHADERFORMAT_DXIL,
        false, NULL);
    zassert_ptr_ne(NULL, dev);
    SDL_GPUTextureCreateInfo info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage =
            SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        .width = 1024,
        .height = 512,
        .layer_count_or_depth = 1,
        .num_levels = 1};
    SDL_GPUTexture* src = SDL_CreateGPUTexture(dev, &info);
    zassert_ptr_ne(NULL, src);
    SDL_GPUTransferBufferCreateInfo ti = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = 1024 * 512 * 4};
    SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(dev, &ti);
    zassert_ptr_ne(NULL, upload);
    u8* pixels = SDL_MapGPUTransferBuffer(dev, upload, false);
    zassert_ptr_ne(NULL, pixels);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 1024; ++x) {
            int i = (y * 1024 + x) * 4;
            pixels[i] = x % 256;
            pixels[i + 1] = ((y % 224) * 71) % 256;
            pixels[i + 2] = (y % 224) & 1 ? 255 : 0;
            pixels[i + 3] = 255;
        }
    SDL_UnmapGPUTransferBuffer(dev, upload);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source = {.transfer_buffer = upload};
    SDL_GPUTextureRegion region = {.texture = src, .w = 1024, .h = 512, .d = 1};
    SDL_UploadToGPUTexture(copy, &source, &region, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    const int sizes[][2] = {{960, 672}, {960, 720}, {1000, 700}, {1296, 907}};
    for (unsigned size = 0; size < 4; ++size) {
        int w = sizes[size][0], h = sizes[size][1];
        info.width = w;
        info.height = h;
        SDL_GPUTexture* dst = SDL_CreateGPUTexture(dev, &info);
        zassert_ptr_ne(NULL, dst);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        ti.size = w * h * 4;
        SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(dev, &ti);
        u8* reference = malloc(w * h * 4);
        unsigned differences = 0;
        SDL_GPUTextureCreateInfo crop_info = info;
        crop_info.width = 320;
        crop_info.height = 224;
        SDL_GPUTexture* crop = SDL_CreateGPUTexture(dev, &crop_info);
        zassert_ptr_ne(NULL, crop);
        for (int staged = 0; staged < 2; ++staged) {
            differences = 0;
            for (int slot = 0; slot < 2; ++slot) {
                cmd = SDL_AcquireGPUCommandBuffer(dev);
                SDL_GPUBlitInfo blit = {
                    .source =
                        {.texture = src, .y = slot * 224, .w = 320, .h = 224},
                    .destination = {.texture = dst, .w = w, .h = h},
                    .load_op = SDL_GPU_LOADOP_DONT_CARE,
                    .filter = SDL_GPU_FILTER_NEAREST};
                if (staged) {
                    copy = SDL_BeginGPUCopyPass(cmd);
                    SDL_GPUTextureLocation from = {.texture = src,
                                                   .y = slot * 224},
                                           to = {.texture = crop};
                    SDL_CopyGPUTextureToTexture(
                        copy, &from, &to, 320, 224, 1, false);
                    SDL_EndGPUCopyPass(copy);
                    blit.source = (SDL_GPUBlitRegion){
                        .texture = crop, .w = 320, .h = 224};
                }
                SDL_BlitGPUTexture(cmd, &blit);
                copy = SDL_BeginGPUCopyPass(cmd);
                source.transfer_buffer = download;
                region = (SDL_GPUTextureRegion){
                    .texture = dst, .w = w, .h = h, .d = 1};
                SDL_DownloadFromGPUTexture(copy, &region, &source);
                SDL_EndGPUCopyPass(copy);
                SDL_GPUFence* fence =
                    SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
                SDL_WaitForGPUFences(dev, true, &fence, 1);
                SDL_ReleaseGPUFence(dev, fence);
                pixels = SDL_MapGPUTransferBuffer(dev, download, false);
                if (!slot)
                    memcpy(reference, pixels, w * h * 4);
                else
                    for (int i = 0; i < w * h * 4; ++i)
                        differences += reference[i] != pixels[i];
                SDL_UnmapGPUTransferBuffer(dev, download);
            }
            printf(
                "Presentation crop %dx%d, staged=%d: %u differing channels\n",
                w, h, staged, differences);
            if (staged)
                zassert_s32_eq(0, differences);
        }
        free(reference);
        SDL_ReleaseGPUTransferBuffer(dev, download);
        SDL_ReleaseGPUTexture(dev, dst);
        SDL_ReleaseGPUTexture(dev, crop);
    }
    SDL_ReleaseGPUTransferBuffer(dev, upload);
    SDL_ReleaseGPUTexture(dev, src);
    SDL_DestroyGPUDevice(dev);
#endif
}

/* Text uses affine FT4s in vertically stacked field buffers. Their texture
 * samples must be independent of the absolute VRAM row of the draw target. */
ZTEST(gpu, text_quad_is_stable_across_field_buffers) {
    zskip_targets("psp;ps1");
#if defined(__psyz) && !defined(__PSP__)
    const unsigned scales[] = {1, 2, 7, 8};
    u16 texels[32 * 16];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x)
            texels[y * 32 + x] = (1 + ((x * 13 + y * 7) % 31)) |
                                 (((x + y) % 31) << 5) | ((y % 31) << 10);
    RECT texture = {512, 0, 32, 16};
    LoadImage(&texture, (u_long*)texels);
    DrawSync(0);
    const char* headless = getenv("PSYZ_HEADLESS");
    unsigned count = headless && !strcmp(headless, "1") ? 1 : 4;
    for (unsigned si = 0; si < count; ++si) {
        unsigned scale = scales[si];
        Psyz_VideoSetInternalResolution(scale);
        unsigned char* reference = NULL;
        int rw = 0, rh = 0;
        for (int slot = 0; slot < 2; ++slot) {
            DRAWENV draw;
            DISPENV disp;
            SetDefDrawEnv(&draw, 0, slot * 224, 320, 224);
            draw.dtd = 0;
            PutDrawEnv(&draw);
            SetDefDispEnv(&disp, 0, slot * 224, 320, 224);
            PutDispEnv(&disp);
            RECT clear = {0, slot * 224, 320, 224};
            ClearImage(&clear, 0, 0, 0);
            POLY_FT4 text = {0};
            setPolyFT4(&text);
            setRGB0(&text, 128, 128, 128);
            setXYWH(&text, 23, 173, 13, 12);
            setUVWH(&text, 3, 2, 13, 12);
            text.tpage = GetTPage(2, 0, 512, 0);
            DrawPrim(&text);
            DR_TPAGE page;
            SetDrawTPage(&page, 0, 0, text.tpage);
            DrawPrim(&page);
            SPRT glyph = {0};
            setSprt(&glyph);
            setRGB0(&glyph, 128, 128, 128);
            setXY0(&glyph, 43, 173);
            setUV0(&glyph, 3, 2);
            setWH(&glyph, 13, 12);
            DrawPrim(&glyph);
            DrawSync(0);
            int w, h;
            unsigned char* image = Psyz_VideoAllocRenderedFrame(&w, &h);
            zassert_ptr_ne(NULL, image);
            if (!slot) {
                reference = image;
                rw = w;
                rh = h;
            } else {
                zassert_s32_eq(rw, w);
                zassert_s32_eq(rh, h);
                unsigned differences = 0;
                for (int i = 0; i < w * h * 3; ++i)
                    differences += image[i] != reference[i];
                printf(
                    "Field-buffer text stability %ux: %u differing channels\n",
                    scale, differences);
                zassert_s32_eq(0, differences);
                free(image);
            }
        }
        free(reference);
    }
    Psyz_VideoSetInternalResolution(1);
#endif
}

ZTEST(gpu, rgb24_display_reads_packed_vram_bytes) {
    const int widths[] = {256, 320, 368, 512, 640};
    for (unsigned mode = 0; mode < sizeof(widths) / sizeof(widths[0]); ++mode) {
        Psyz_VideoSetInternalResolution(mode == 4 ? 2 : 1);
        int width = widths[mode], words = (width * 3 + 1) / 2;
        u16* image = malloc((size_t)words * 240 * 2);
        zassert_ptr_ne(NULL, image);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < words; ++x)
                image[y * words + x] = (u16)((x * 197 + y * 911) ^ 0xace1);
        RECT rect = {3, 240, words, 240};
        LoadImage(&rect, (u_long*)image);
        DrawSync(0);
        DISPENV display;
        SetDefDispEnv(&display, 3, 240, width, 240);
        display.isrgb24 = 1;
        PutDispEnv(&display);
        VSync(0); /* Exercise the packed-RGB presentation path as well as
                     readback. */
        int w, h;
        unsigned char* pixels = Psyz_VideoAllocCapturedFrame(&w, &h);
        zassert_ptr_ne(NULL, pixels);
        zassert_s32_eq(width, w);
        zassert_s32_eq(240, h);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < width * 3; ++x)
                zassert_u8_eq((u8)(image[y * words + x / 2] >> ((x & 1) * 8)),
                              pixels[y * width * 3 + x]);
        free(pixels);
        free(image);
    }
    Psyz_VideoSetInternalResolution(1);
}

ZTEST(gpu, display_vertical_range_excludes_adjacent_vram_rows) {
    const int heights[] = {224, 240, 224, 240};
    for (unsigned mode = 0; mode < 4; ++mode) {
        int height = heights[mode];
        int words = mode >= 2 ? 480 : 320;
        u16* image = calloc((size_t)words * 240, sizeof(*image));
        zassert_ptr_ne(NULL, image);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x) {
                if (mode >= 2) {
                    unsigned char* pixel =
                        (unsigned char*)(image + y * words) + x * 3;
                    pixel[y < height ? 0 : 1] = 255;
                } else
                    image[y * words + x] = y < height ? 0x001f : 0x03e0;
            }
        RECT region = {0, 224, words, 240};
        LoadImage(&region, (u_long*)image);
        DrawSync(0);
        DISPENV display;
        SetDefDispEnv(&display, 0, 224, 320, height);
        display.screen.y = height == 224 ? 8 : 0;
        display.screen.h = height;
        display.isrgb24 = mode >= 2;
        PutDispEnv(&display);
        VSync(0);
        int width, captured_height;
        unsigned char* pixels =
            Psyz_VideoAllocCapturedFrame(&width, &captured_height);
        zassert_ptr_ne(NULL, pixels);
        zassert_s32_eq(320, width);
        zassert_s32_eq(height, captured_height);
        for (int i = 0; i < width * height; ++i) {
            zassert_u8_eq(255, pixels[i * 3]);
            zassert_u8_eq(0, pixels[i * 3 + 1]);
            zassert_u8_eq(0, pixels[i * 3 + 2]);
        }
        free(pixels);
        free(image);
    }
}

ZTEST(gpu, rotated_triangle_samples_signed_affine_uv) {
    u16 texels[16 * 16];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            texels[y * 16 + x] = (1 + x) | ((1 + y) << 5) | (3 << 10);
    RECT texture = {512, 0, 16, 16};
    LoadImage(&texture, (u_long*)texels);
    DrawSync(0);
    const u8 uv[][6] = {{13, 2, 2, 13, 12, 12},
                        {2, 13, 13, 2, 12, 12},
                        {2, 12, 12, 2, 13, 13},
                        {13, 12, 12, 13, 2, 2}};
    /* Independent GPU readback, including the half-unit polygon bias. */
    static const u16 expected[4][5] = {
        {0x0cec, 0x0ceb, 0x0d0a, 0x0d0c, 0x0d49},
        {0x0d87, 0x0d67, 0x0d48, 0x0d88, 0x0d2a},
        {0x0d67, 0x0d67, 0x0d48, 0x0d88, 0x0d2a},
        {0x0d6b, 0x0d6c, 0x0d8c, 0x0d4a, 0x0d6b},
    };
    const short samples[][2] = {
        {23, 24}, {24, 23}, {28, 22}, {22, 28}, {30, 25}};
    for (unsigned mode = 0; mode < 4; ++mode) {
        POLY_FT3 triangle;
        SetPolyFT3(&triangle);
        setShadeTex(&triangle, 1);
        setXY3(&triangle, 16, 16, 48, 20, 20, 48);
        setUV3(&triangle, uv[mode][0], uv[mode][1], uv[mode][2], uv[mode][3],
               uv[mode][4], uv[mode][5]);
        triangle.tpage = GetTPage(2, 0, 512, 0);
        triangle.clut = 0;
        DrawPrim(&triangle);
        DrawSync(0);
        for (unsigned sample = 0; sample < 5; ++sample) {
            int x = samples[sample][0], y = samples[sample][1];
            u16 pixel;
            RECT point = {x, y, 1, 1};
            StoreImage(&point, (u_long*)&pixel);
            DrawSync(0);
            zassert_u16_eq(expected[mode][sample], pixel & 0x7fff);
        }
    }
}

ZTEST(gpu, rendered_capture_contains_internal_detail) {
    zskip_targets("psp;ps1");
#if defined(__psyz) && !defined(__PSP__)
    int w = 7, h = 9;
    zassert_ptr_eq(NULL, Psyz_VideoAllocRenderedFrame(NULL, &h));
    zassert_s32_eq(0, h);
    zassert_ptr_eq(NULL, Psyz_VideoAllocRenderedFrame(&w, NULL));
    zassert_s32_eq(0, w);
    const char* headless = getenv("PSYZ_HEADLESS");
    unsigned count = headless && !strcmp(headless, "1") ? 1 : 4;
    const unsigned scales[4] = {1, 2, 3, 8};
    static u16 before[320 * 280], after[320 * 280];
    RECT check = {0, 0, 320, 280};
    DISPENV display;
    SetDefDispEnv(&display, 16, 24, 256, 240);
    PutDispEnv(&display);
    for (unsigned item = 0; item < count; ++item) {
        unsigned scale = scales[item];
        zassert_s32_eq(0, Psyz_VideoSetInternalResolution(scale));
        ClearImage(&check, 0, 0, 0);
        POLY_F3 triangle = {0};
        setPolyF3(&triangle);
        setRGB0(&triangle, 255, 255, 255);
        setXY3(&triangle, 20, 28, 70, 37, 29, 65);
        DrawPrim(&triangle);
        u8* rendered = Psyz_VideoAllocRenderedFrame(&w, &h);
        zassert_ptr_ne(NULL, rendered);
        zassert_s32_eq(256 * scale, w);
        zassert_s32_eq(240 * scale, h);
        StoreImage(&check, (u_long*)before);
        DrawSync(0);
        int nw, nh;
        u8* native = Psyz_VideoAllocCapturedFrame(&nw, &nh);
        zassert_ptr_ne(NULL, native);
        zassert_s32_eq(256, nw);
        zassert_s32_eq(240, nh);
        unsigned detail = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (memcmp(rendered + ((size_t)y * w + x) * 3,
                           native + ((size_t)(y / scale) * nw + x / scale) * 3,
                           3))
                    ++detail;
        if (scale == 1)
            zassert_s32_eq(0, detail);
        else
            zassert_s32_eq(1, detail > 0);
        for (unsigned c = 0; c < 3; ++c) {
            zassert_u8_eq(255, rendered[((size_t)(16 * scale + scale / 2) * w +
                                         19 * scale + scale / 2) *
                                            3 +
                                        c]);
            zassert_u8_eq(
                0, rendered[((size_t)(180 * scale) * w + 200 * scale) * 3 + c]);
        }
        int rw, rh;
        u8* repeated = Psyz_VideoAllocRenderedFrame(&rw, &rh);
        zassert_ptr_ne(NULL, repeated);
        zassert_s32_eq(w, rw);
        zassert_s32_eq(h, rh);
        zassert_s32_eq(0, memcmp(rendered, repeated, (size_t)w * h * 3));
        StoreImage(&check, (u_long*)after);
        DrawSync(0);
        zassert_s32_eq(0, memcmp(before, after, sizeof(before)));
        printf("Rendered capture %ux: %dx%d, %u subpixel edge samples, native "
               "VRAM preserved\n",
               scale, w, h, detail);
        free(native);
        free(rendered);
        free(repeated);
    }
    display.isrgb24 = 1;
    PutDispEnv(&display);
    int nw, nh;
    u8* native = Psyz_VideoAllocCapturedFrame(&nw, &nh);
    u8* rendered = Psyz_VideoAllocRenderedFrame(&w, &h);
    zassert_ptr_ne(NULL, native);
    zassert_ptr_ne(NULL, rendered);
    zassert_s32_eq(nw, w);
    zassert_s32_eq(nh, h);
    zassert_s32_eq(0, memcmp(native, rendered, (size_t)w * h * 3));
    free(native);
    free(rendered);
    display.isrgb24 = 0;
    PutDispEnv(&display);
    Psyz_VideoSetInternalResolution(1);
    SetDefDispEnv(&display, 1010, 500, 256, 240);
    PutDispEnv(&display);
    w = h = 7;
    zassert_ptr_eq(NULL, Psyz_VideoAllocRenderedFrame(&w, &h));
    zassert_s32_eq(0, w);
    zassert_s32_eq(0, h);
#endif
}

ZTEST(gpu, thin_triangle_integer_attribute_boundary) {
    static u16 texels[256 * 128];
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 256; ++x)
            texels[y * 256 + x] =
                (x & 31) | ((y & 31) << 5) | (((x + y) & 31) << 10) | 0x8000;
    RECT texture = {512, 0, 256, 128};
    LoadImage(&texture, (u_long*)texels);
    DrawSync(0);
    POLY_GT3 triangle = {0};
    setPolyGT3(&triangle);
    setXY3(&triangle, 190, 88, 187, 89, 187, 87);
    setUV3(&triangle, 236, 100, 246, 115, 251, 100);
    setRGB0(&triangle, 87, 87, 87);
    setRGB1(&triangle, 107, 107, 107);
    setRGB2(&triangle, 112, 112, 112);
    triangle.tpage = GetTPage(2, 0, 512, 0);
    DrawPrim(&triangle);
    DrawSync(0);
    u16 pixel = 0;
    RECT sample = {189, 88, 1, 1};
    StoreImage(&sample, (u_long*)&pixel);
    DrawSync(0);
    /* Signed 12-bit gradients give U=240, V=103 and RGB=95 here.
     * V and RGB land exactly on integer boundaries, not just nearby. */
    zexpect_u16_eq(0xc4ab, pixel);
    setXY3(&triangle, 71, 29, 15, 103, 27, 152);
    setUV3(&triangle, 10, 36, 10, 30, 10, 77);
    setRGB0(&triangle, 128, 128, 128);
    setRGB1(&triangle, 128, 128, 128);
    setRGB2(&triangle, 128, 128, 128);
    DrawPrim(&triangle);
    DrawSync(0);
    sample = (RECT){37, 94, 1, 1};
    StoreImage(&sample, (u_long*)&pixel);
    DrawSync(0);
    /* V=46+4095/4096 must stay below 47: a whole-LSB or larger
     * interpolation bias would fix the first case but break this one. */
    zexpect_u16_eq(0xe1ca, pixel);
}

ZTEST(gpu, drawprim_packed_sprite_and_native_commands) {
    u16 palette[16] = {0, 0xC210};
    u16 texture = 0x1111;
    RECT palette_rect = {0, 480, 16, 1}, texture_rect = {512, 256, 1, 1};
    LoadImage(&palette_rect, (u_long*)palette);
    LoadImage(&texture_rect, (u_long*)&texture);
    DrawSync(0);
    DR_TPAGE page;
    DR_AREA area;
    RECT clip = {13, 14, 2, 1};
    SPRT sprite = {0};
    SetDrawTPage(&page, 0, 0, GetTPage(0, 0, 512, 256));
    SetDrawArea(&area, &clip);
    setSprt(&sprite);
    setRGB0(&sprite, 128, 128, 128);
    setXY0(&sprite, 12, 14);
    setUV0(&sprite, 0, 0);
    setWH(&sprite, 4, 1);
    sprite.clut = GetClut(0, 480);
    DrawPrim(&page);
    DrawPrim(&area);
    DrawPrim(&sprite);
    DrawSync(0);
    RECT result_rect = {12, 14, 4, 1};
    u16 result[4] = {0};
    StoreImage(&result_rect, (u_long*)result);
    DrawSync(0);
    zexpect_u32_eq(0, result[0]);
    zexpect_u32_eq(0xC210, result[1]);
    zexpect_u32_eq(0xC210, result[2]);
    zexpect_u32_eq(0, result[3]);
}

ZTEST(gpu, merged_native_commands_sprite_direct_ot_and_copies) {
    u16 palette[16] = {0, 0xC210}, texture = 0x1111;
    RECT palette_rect = {0, 480, 16, 1}, texture_rect = {512, 256, 1, 1};
    RECT result_rect = {12, 14, 4, 1}, window = {0, 0, 0, 0};
    struct {
        union {
            DR_TPAGE page;
            DR_MODE mode;
        } command;
        SPRT sprite;
    } packet, copy;
    for (int kind = 0; kind < 2; ++kind) {
        memset(&packet, 0xA5, sizeof(packet));
        if (kind)
            SetDrawMode(
                &packet.command.mode, 0, 0, GetTPage(0, 0, 512, 256), &window);
        else
            SetDrawTPage(&packet.command.page, 0, 0, GetTPage(0, 0, 512, 256));
        setSprt(&packet.sprite);
        setRGB0(&packet.sprite, 128, 128, 128);
        setXY0(&packet.sprite, 12, 14);
        setUV0(&packet.sprite, 0, 0);
        setWH(&packet.sprite, 4, 1);
        packet.sprite.clut = GetClut(0, 480);
        zexpect_s32_eq(0, MargePrim(&packet.command, &packet.sprite));
        memcpy(&copy, &packet, sizeof(copy));
        for (int path = 0; path < 4; ++path) {
            ClearImage(&result_rect, 0, 0, 0);
            LoadImage(&palette_rect, (u_long*)palette);
            LoadImage(&texture_rect, (u_long*)&texture);
            DrawSync(0);
            void* head = path & 1 ? (void*)&copy : (void*)&packet;
            if (path < 2)
                DrawPrim(head);
            else {
                /* P_TAG also includes the first payload word. Keep that word
                 * addressable when addPrim accesses the final native OT tag. */
                OT_TYPE ot[3];
                ClearOTagR(ot, 2);
                addPrim(ot + 1, head);
                DrawOTag(ot + 1);
                DrawSync(0);
            }
            u16 result[4] = {0};
            DrawSync(0);
            StoreImage(&result_rect, (u_long*)result);
            DrawSync(0);
            for (int i = 0; i < 4; ++i)
                zexpect_u32_eq(0xC210, result[i]);
        }
    }
}

ZTEST(gpu, fnt_print) {
    FntLoad(960, 256);
    SetDumpFnt(FntOpen(4, 4, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 512));
    ClearImage(&cdb->draw.clip, 60, 120, 120);
    FntPrint("hello psyz!");
    FntFlush(-1);
    DrawSync(0);
    VSync(0);
    PutDrawEnv(&cdb->draw);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("fnt_print", 0, 1.0f);
}

ZTEST(gpu, draw_ft4) {
    u_short tpage, clut;
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }
    SetPolyFT4(&cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 16, 16, 64, 64);
    setRGB0(&cdb->ft4[0], 128, 128, 128);
    setUVWH(&cdb->ft4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = clut;

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].ft4[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("draw_ft4", 0, 1.0f);
}

ZTEST(gpu, draw_ft4_colored) {
    u_short tpage, clut;
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }
    SetPolyFT4(&cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 16, 16, 64, 64);
    setRGB0(&cdb->ft4[0], 255, 128, 128);
    setUVWH(&cdb->ft4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = clut;

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].ft4[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("draw_ft4_colored", 0, 1.0f);
}

ZTEST(gpu, draw_gt4) {
    zskip_targets("pcsx-redux"); // differs from real hardware
    u_short tpage, clut;
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }
    SetPolyGT4(&cdb->gt4[0]);
    setXYWH(&cdb->gt4[0], 16, 16, 64, 64);
    setRGB0(&cdb->gt4[0], 128, 0, 0);
    setRGB1(&cdb->gt4[0], 0, 128, 0);
    setRGB2(&cdb->gt4[0], 0, 0, 128);
    setRGB3(&cdb->gt4[0], 128, 128, 0);
    setUVWH(&cdb->gt4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->gt4[0], 0);
    cdb->gt4[0].tpage = tpage;
    cdb->gt4[0].clut = clut;

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].gt4[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("draw_gt4", 1, 1.0f);
}

ZTEST(gpu, draw_sprt_8bpp) {
    TIM_IMAGE tim;
    zassert_s32_eq(0, OpenTIM((u_long*)img_4bpp));
    zassert_ptr_ne(NULL, ReadTIM(&tim));

    static u_short img8bpp[128 * 256];
    RECT rect = *tim.prect;
    u_char* src = (u_char*)tim.paddr;
    u_char* dst = (u_char*)img8bpp;
    int i;
    for (i = 0; i < rect.w * rect.h * 2; i++) {
        dst[i * 2 + 0] = src[i] & 15;
        dst[i * 2 + 1] = src[i] >> 4;
    }
    rect.w *= 2;
    LoadImage(&rect, (u_long*)img8bpp);
    LoadImage(tim.crect, tim.caddr);

    u_short tpage, clut;
    tpage = GetTPage(1, 0, rect.x, rect.y);
    clut = GetClut(tim.crect->x, tim.crect->y);

    SetSprt(&cdb->sprt[0]);
    setShadeTex(&cdb->sprt[0], 1);
    setXY0(&cdb->sprt[0], 16, 16);
    setWH(&cdb->sprt[0], rect.w * 2, rect.h);
    setUV0(&cdb->sprt[0], 0, 0);
    cdb->sprt[0].clut = clut;
    SetDrawMode(&cdb->drmode[0], 0, 0, tpage, NULL);
    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &cdb->sprt[0]);
    ClearImage(&cdb->draw.clip, 60, 120, 120);
    AddPrim(cdb->ot, &cdb->drmode[0]);
    Present("draw_sprt_8bpp");
}

ZTEST(gpu, draw_sprt_16bpp) {
    u_short tpage, clut;
    zassert_s32_eq(0, LoadTim(img_16bpp, &tpage, &clut));
    SetSprt(&cdb->sprt[0]);
    setShadeTex(&cdb->sprt[0], 1);
    setXY0(&cdb->sprt[0], 16, 16);
    setWH(&cdb->sprt[0], 64, 64);
    setUV0(&cdb->sprt[0], 0, 0);
    SetDrawMode(&cdb->drmode[0], 0, 0, tpage, NULL);
    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &cdb->sprt[0]);
    ClearImage(&cdb->draw.clip, 60, 120, 120);
    AddPrim(cdb->ot, &cdb->drmode[0]);
    Present("draw_sprt_16bpp");
}

ZTEST(gpu, draw_sprt_16bpp_page_past_vram_edge) {
    TIM_IMAGE tim;
    zassert_s32_eq(0, OpenTIM((u_long*)img_16bpp));
    zassert_ptr_ne(NULL, ReadTIM(&tim));
    RECT rect = *tim.prect;
    rect.x = 960;
    rect.y = 0;
    LoadImage(&rect, tim.paddr);
    SetSprt(&cdb->sprt[0]);
    setShadeTex(&cdb->sprt[0], 1);
    setXY0(&cdb->sprt[0], 16, 16);
    setWH(&cdb->sprt[0], 64, 64);
    setUV0(&cdb->sprt[0], 128, 0);
    SetDrawMode(&cdb->drmode[0], 0, 0, GetTPage(2, 0, 832, 0), NULL);
    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &cdb->sprt[0]);
    ClearImage(&cdb->draw.clip, 60, 120, 120);
    AddPrim(cdb->ot, &cdb->drmode[0]);
    Present("draw_sprt_16bpp");
}

ZTEST(gpu, read_tim_walks_consecutive_images) {
    static unsigned int tims[][6] = {
        {0x10, 2, 16, 0x00100100, 0x00020001, 0x7FFF7FFF},
        {0x10, 2, 16, 0x00200200, 0x00020001, 0x001F001F},
        {0},
    };
    TIM_IMAGE tim;
    zassert_s32_eq(0, OpenTIM((u_long*)tims));
    zassert_ptr_ne(NULL, ReadTIM(&tim));
    zexpect_ptr_eq(&tims[0][3], tim.prect);
    zexpect_ptr_eq(&tims[0][5], tim.paddr);
    zassert_ptr_ne(NULL, ReadTIM(&tim));
    zexpect_ptr_eq(&tims[1][3], tim.prect);
    zexpect_ptr_eq(&tims[1][5], tim.paddr);
    zexpect_s32_eq(0x200, tim.prect->x);
    zexpect_s32_eq(0x20, tim.prect->y);
    zexpect_ptr_eq(NULL, ReadTIM(&tim));
}

ZTEST(gpu, gouraud_line_after_flush) {
    int w, h;
    unsigned char* d;
    const unsigned char* p87;
    const unsigned char* p88;
    const unsigned char* p;

    SetPolyF4(&cdb->f4[0]);
    setXYWH(&cdb->f4[0], 16, 16, 64, 64);
    setRGB0(&cdb->f4[0], 0, 0, 255);
    setSemiTrans(&cdb->f4[0], 0);

    SetLineG2(&cdb->lineg2[0]);
    setXY2(&cdb->lineg2[0], 16, 88, 80, 88);
    setRGB0(&cdb->lineg2[0], 255, 0, 0);
    setRGB1(&cdb->lineg2[0], 0, 255, 0);
    setSemiTrans(&cdb->f4[0], 0);

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].lineg2[0]);
    AddPrim(cdb->ot, &db[0].f4[0]);

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    d = Psyz_VideoAllocCapturedFrame(&w, &h);
    zassert_ptr_ne(NULL, d);

    // Linux and Windows renders the line at y:87, macOS does it at y:88
    p87 = d + 3 * (87 * w + 48);
    p88 = d + 3 * (88 * w + 48);

    p = (p87[0] + p87[1] > p87[2]) ? p87 : p88;
    zprintf("line should be red/green mix, not blue\n");
    zexpect_s32_gt(p[2], p[0] + p[1]);
    zprintf("line color lost after flush\n");
    zexpect_s32_gt(128, p[0] + p[1]);
    zprintf("line B should be near zero\n");
    zexpect_s32_lt(64, p[2]);
    free(d);
}

ZTEST(gpu, draw_lines) {
    zskip_targets("pcsx-redux"); // differs from real hardware
    zskip_targets("ppsspp");
    ClearImage(&cdb->draw.clip, 0, 0, 0);
    ClearOTag(cdb->ot, OTSIZE);

    SetLineG2(&cdb->lineg2[0]);
    setXY2(&cdb->lineg2[0], 16, 40, 112, 40);
    setRGB0(&cdb->lineg2[0], 255, 0, 0);
    setRGB1(&cdb->lineg2[0], 0, 255, 0);
    AddPrim(cdb->ot, &cdb->lineg2[0]);

    SetLineG3(&cdb->lineg3[0]);
    setXY3(&cdb->lineg3[0], 16, 80, 64, 120, 112, 80);
    setRGB0(&cdb->lineg3[0], 255, 0, 0);
    setRGB1(&cdb->lineg3[0], 0, 255, 0);
    setRGB2(&cdb->lineg3[0], 0, 0, 255);
    AddPrim(cdb->ot, &cdb->lineg3[0]);

    SetLineG4(&cdb->lineg4[0]);
    setXY4(&cdb->lineg4[0], 16, 150, 16, 200, 112, 200, 112, 150);
    setRGB0(&cdb->lineg4[0], 255, 0, 0);
    setRGB1(&cdb->lineg4[0], 0, 255, 0);
    setRGB2(&cdb->lineg4[0], 0, 0, 255);
    setRGB3(&cdb->lineg4[0], 255, 255, 0);
    AddPrim(cdb->ot, &cdb->lineg4[0]);

    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    // PSP has its own golden: native line geometry has a different
    // rasterization algorithm, and using triangles for lines is too expensive
    // for the GE.
    ASSERT_FRAME("draw_lines", 1, 0.9993f);
}

ZTEST(gpu, set_draw_area) {
    u_short tpage, clut;
    DR_AREA drArea;
    RECT area = {4, 8, 56, 48};
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }

    SetPolyFT4(&cdb->ft4[0]);
    AddPrim(cdb->ot, &cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 0, 0, 64, 64);
    setRGB0(&cdb->ft4[0], 128, 128, 128);
    setUVWH(&cdb->ft4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = clut;

    AddPrim(cdb->ot, &drArea);
    SetDrawArea(&drArea, &area);

    setRECT(&cdb->draw.clip, 32, 24, 160, 128);
    ClearImage(&cdb->draw.clip, 60, 120, 120);
    setRECT(&cdb->draw.clip, 128, 128, 64, 64);
    ClearImage(&cdb->draw.clip, 120, 120, 60);

    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("set_draw_area", 0, 1.0f);
}

ZTEST(gpu, swap_buffer) {
    u_short tpage, clut;
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }

    for (int fbidx = 0; fbidx < 2; fbidx++) {
        cdb = &db[fbidx & 1];
        cdb->draw.isbg = 1;
        cdb->draw.tpage = tpage;
        setRGB0(&cdb->draw, 60, 120, 120);
        PutDrawEnv(&cdb->draw);
        PutDispEnv(&cdb->disp);

        SetSprt(cdb->sprt);
        SetSemiTrans(cdb->sprt, 0);
        SetShadeTex(cdb->sprt, 1);
        setXY0(cdb->sprt, 0, fbidx * 16);
        setWH(cdb->sprt, 64, 64);
        setUV0(cdb->sprt, 0, 0);
        cdb->sprt[0].clut = clut;
        ClearOTag(cdb->ot, OTSIZE);
        AddPrim(cdb->ot, cdb->sprt);
        DrawOTag(cdb->ot);

        DrawSync(0);
        VSync(0);
        ASSERT_FRAME(
            (fbidx & 1) ? "swap_buffer_fb2" : "swap_buffer_fb1", 0, 1.0f);
    }
}

ZTEST(gpu, drawenv_clear_vram) {
    const char* ci = getenv("CI");
    const char* os = getenv("OS");
    u_short tpage, clut;
    DRAWENV drawEnv;
    if (ci && strcmp(ci, "1") == 0 && os && strcmp(os, "linux") == 0) {
        zskip("Skipped on Linux CI");
    }
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }

    memset(&drawEnv, 0, sizeof(drawEnv));
    drawEnv.clip.x = 964;
    drawEnv.clip.y = 16;
    drawEnv.clip.w = 8;
    drawEnv.clip.h = 32;
    drawEnv.ofs[0] = drawEnv.clip.x;
    drawEnv.ofs[1] = drawEnv.clip.y;
    drawEnv.r0 = 255;
    drawEnv.g0 = drawEnv.b0 = 0;
    drawEnv.isbg = 1;
    PutDrawEnv(&drawEnv);

    cdb->draw.tpage = tpage;
    PutDrawEnv(&cdb->draw);

    SetPolyFT4(&cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 16, 16, 64, 64);
    setRGB0(&cdb->ft4[0], 128, 128, 128);
    setUVWH(&cdb->ft4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = clut;

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].ft4[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    if (ztest_is_target("psp")) {
        ASSERT_FRAME("drawenv_clear_vram", 2, 0.995f);
    } else {
        ASSERT_FRAME("drawenv_clear_vram", 0, 1.0f);
    }
}

ZTEST(gpu, move_image) {
    u_short tpage, clut;
    RECT rect = {16, 16, 64, 64};
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }

    SetPolyFT4(&cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 16, 16, 64, 64);
    setRGB0(&cdb->ft4[0], 128, 128, 128);
    setUVWH(&cdb->ft4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = clut;

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].ft4[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);

    MoveImage(&rect, 144, 144);
    DrawSync(0);

    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("move_image", 0, 1.0f);
}

ZTEST(gpu, move_image_overlap) {
    u_short tpage, clut;
    RECT rect = {960, 0, 16, 64};
    zskip_targets("psp");
    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }

    MoveImage(&rect, 962, 8);
    rect.x = 962;
    rect.y = 8;
    MoveImage(&rect, 960, 0);

    SetPolyFT4(&cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 16, 16, 64, 64);
    setRGB0(&cdb->ft4[0], 128, 128, 128);
    setUVWH(&cdb->ft4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = clut;

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].ft4[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("move_image_overlap", 0, 1.0f);
}

ZTEST(gpu, rgb555_blend_rounding_and_overlap) {
    enum { width = 128, height = 32 };
    static u16 background[width * height], actual[width * height];
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned dst = 0; dst < 32; ++dst)
            for (unsigned src = 0; src < 32; ++src)
                background[dst * width + mode * 32 + src] =
                    dst | (((dst * 5 + 3) & 31) << 5) |
                    (((dst * 7 + 11) & 31) << 10);
    RECT region = {0, 0, width, height};
    LoadImage(&region, (u_long*)background);
    DrawSync(0);
    for (unsigned layer = 0; layer < 2; ++layer) {
        for (unsigned mode = 0; mode < 4; ++mode) {
            Psyz_GpuWriteGP0(_get_mode(1, 0, getTPage(0, mode, 0, 0)));
            for (unsigned dst = 0; dst < 32; ++dst)
                for (unsigned src = 0; src < 32; ++src) {
                    unsigned value = layer ? 31 - src : src;
                    unsigned color[3] = {value, (value + 7) & 31,
                                         (value * 3 + 5) & 31};
                    POLY_F4 quad = {0};
                    setPolyF4(&quad);
                    setSemiTrans(&quad, 1);
                    setXYWH(&quad, mode * 32 + src, dst, 1, 1);
                    setRGB0(&quad, color[0] * 8, color[1] * 8, color[2] * 8);
                    DrawPrim(&quad);
                    unsigned index = dst * width + mode * 32 + src, result = 0;
                    for (unsigned c = 0; c < 3; ++c) {
                        int b = (background[index] >> (5 * c)) & 31,
                            f = color[c], v;
                        if (mode == 0)
                            v = (b + f) >> 1;
                        else if (mode == 1)
                            v = b + f;
                        else if (mode == 2)
                            v = b - f;
                        else
                            v = b + (f >> 2);
                        if (v < 0)
                            v = 0;
                        if (v > 31)
                            v = 31;
                        result |= (unsigned)v << (5 * c);
                    }
                    background[index] = result;
                    /* Retire cycled upload buffers periodically. The test
                     * needs all color combinations, not thousands of GPU
                     * staging buffers alive together on a software adapter. */
                    if ((index & 127) == 127) {
                        DrawSync(0);
                        StoreImage(&region, (u_long*)actual);
                        DrawSync(0);
                    }
                }
        }
        DrawSync(0);
        StoreImage(&region, (u_long*)actual);
        DrawSync(0);
        for (unsigned i = 0; i < width * height; ++i)
            zexpect_u16_eq(background[i], actual[i]);
    }
}

ZTEST(gpu, polygon_attribute_batch_boundaries) {
    enum { width = 168, height = 104 };
    static u16 expected[width * height], actual[width * height];
    memset(expected, 0, sizeof(expected));
    RECT region = {0, 0, width, height};
    ClearImage(&region, 0, 0, 0);
    DrawSync(0);
    for (unsigned i = 0; i < 1025; ++i) {
        POLY_G4 quad = {0};
        u8 r = (i & 31) * 8, g = ((i * 7) & 31) * 8,
           b = ((i * 13 + 1) & 31) * 8;
        int x = (i % 41) * 4, y = (i / 41) * 4;
        setPolyG4(&quad);
        setXYWH(&quad, x, y, 2, 2);
        setRGB0(&quad, r, g, b);
        setRGB1(&quad, r, g, b);
        setRGB2(&quad, r, g, b);
        setRGB3(&quad, r, g, b);
        DrawPrim(&quad);
        u16 color = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
        expected[y * width + x] = expected[y * width + x + 1] = color;
        expected[(y + 1) * width + x] = expected[(y + 1) * width + x + 1] =
            color;
    }
    DrawSync(0);
    StoreImage(&region, (u_long*)actual);
    DrawSync(0);
    for (unsigned i = 0; i < width * height; ++i)
        zexpect_u16_eq(expected[i], actual[i]);
}

ZTEST(gpu, clear_image_quantizes_rgb555_before_blending) {
    enum { width = 258, height = 6 };
    u16 expected[width * height], actual[width * height];
    RECT region = {400, 300, width, height};
    for (int i = 0; i < width * height; ++i)
        expected[i] = 0x5a5a;
    LoadImage(&region, (u_long*)expected);
    DrawSync(0);
    for (int row = 0; row < 4; ++row) {
        for (int value = 0; value < 256; ++value) {
            u8 r = row == 0 || row == 3 ? value : 0;
            u8 g = row == 1 ? value : row == 3 ? 255 - value : 0;
            u8 b = row == 2 ? value : row == 3 ? value ^ 0x55 : 0;
            RECT pixel = {401 + value, 301 + row, 1, 1};
            ClearImage(&pixel, r, g, b);
            expected[(row + 1) * width + value + 1] =
                (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
        }
    }
    DrawSync(0);
    StoreImage(&region, (u_long*)actual);
    DrawSync(0);
    for (int i = 0; i < width * height; ++i)
        zexpect_u16_eq(expected[i], actual[i]);
}

ZTEST(gpu, move_image_internal_res) {
    u_short tpage, clut;
    RECT rect = {16, 16, 64, 64};
    u_short pattern[16 * 16];
    u_short readback[16 * 16];
    RECT rectStore = {704, 320, 16, 16};
    zskip_targets("psp;ps1");
    zassert_s32_eq(-1, Psyz_VideoSetInternalResolution(0));
    zassert_s32_eq(0, Psyz_VideoSetInternalResolution(2));
    zassert_s32_eq(2, Psyz_VideoGetInternalResolution());

    if (LoadTim(img_4bpp, &tpage, &clut)) {
        return;
    }

    SetPolyFT4(&cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 16, 16, 64, 64);
    setRGB0(&cdb->ft4[0], 128, 128, 128);
    setUVWH(&cdb->ft4[0], 0, 0, 64, 64);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = clut;

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].ft4[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);

    MoveImage(&rect, 144, 144);
    DrawSync(0);

    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("move_image", 0, 1.0f);

    for (int i = 0; i < 16 * 16; i++) {
        pattern[i] = (u_short)(i * 0x1235);
    }
    LoadImage(&rectStore, (u_long*)pattern);
    DrawSync(0);
    StoreImage(&rectStore, (u_long*)readback);
    DrawSync(0);
    zexpect_u8array_eq(pattern, readback, sizeof(pattern));

    zassert_s32_eq(0, Psyz_VideoSetInternalResolution(1));
    zassert_s32_eq(1, Psyz_VideoGetInternalResolution());
    VSync(0);
    ASSERT_FRAME("move_image", 0, 1.0f);
#if defined(__psyz) && !defined(__PSP__)
    const char* headless = getenv("PSYZ_HEADLESS");
    if (headless && strcmp(headless, "gpu") == 0) {
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        zassert_s32_eq(1, count);
        zassert_s32_eq(1, windows != NULL);
        SDL_WindowFlags flags = SDL_GetWindowFlags(windows[0]);
        SDL_free(windows);
        zassert_s32_eq(1, (flags & SDL_WINDOW_HIDDEN) != 0);
        zassert_s32_eq(PSYZ_VSYNC_LIMITLESS, Psyz_VideoGetVsyncMode());
    }
#endif
}

ZTEST(gpu, disp_mask_preserves_vram) {
    u_short pattern[16 * 16];
    u_short readback[16 * 16];
    RECT rect = {704, 320, 16, 16};
    for (int i = 0; i < 16 * 16; i++) {
        pattern[i] = (u_short)(i * 0x1235);
    }
    LoadImage(&rect, (u_long*)pattern);
    DrawSync(0);

    SetDispMask(0);
    VSync(0);
    DrawSync(0);
    memset(readback, 0, sizeof(readback));
    StoreImage(&rect, (u_long*)readback);
    DrawSync(0);
    zprintf("SetDispMask(0) must not alter VRAM\n");
    zexpect_u8array_eq(pattern, readback, sizeof(pattern));

    SetDispMask(1);
    VSync(0);
    memset(readback, 0, sizeof(readback));
    StoreImage(&rect, (u_long*)readback);
    DrawSync(0);
    zprintf("VRAM must survive the display being re-enabled\n");
    zexpect_u8array_eq(pattern, readback, sizeof(pattern));
}

ZTEST(gpu, blit) {
    TIM_IMAGE tim;
    RECT rect = {16, 16, 64, 64};
    OpenTIM((u_long*)img_16bpp);
    ReadTIM(&tim);
    LoadImage(&rect, tim.paddr);
    VSync(0);
    ASSERT_FRAME("blit", 0, 1.0f);
}

ZTEST(gpu, draw_disp_env) {
    // Can't draw on the same buffer that is also displayed
    zskip_targets("psp");

    // Set different buffers for draw and disp
    SetDefDrawEnv(&db[0].draw, 0, 0, 256, 240);
    SetDefDispEnv(&db[0].disp, 256, 0, 256, 240);
    SetDefDrawEnv(&db[1].draw, 256, 0, 256, 240);
    SetDefDispEnv(&db[1].disp, 0, 0, 256, 240);

    // Ensure buffer 0 is clear
    PutDrawEnv(&db[0].draw);
    PutDispEnv(&db[0].disp);
    ClearImage(&db[0].draw.clip, 0, 0, 0);
    DrawSync(0);
    VSync(0);

    // Ensure buffer 1 is filled with color red
    PutDrawEnv(&db[1].draw);
    PutDispEnv(&db[1].disp);
    ClearImage(&db[1].draw.clip, 0, 0xFF, 0);
    DrawSync(0);
    VSync(0);

    // Back buffer filled with color red, front displays green
    PutDrawEnv(&db[0].draw);
    PutDispEnv(&db[0].disp);
    ClearImage(&db[0].draw.clip, 0xFF, 0, 0);
    DrawSync(0);
    VSync(0);
    ASSERT_FRAME("draw_disp_env_0", 0, 1.0f);

    // Back buffer now becomes front buffer, displays red
    PutDispEnv(&db[1].disp);
    DrawSync(0);
    VSync(0);
    ASSERT_FRAME("draw_disp_env_1", 0, 1.0f);

    // Front buffer is swapped again, display green
    PutDispEnv(&db[0].disp);
    DrawSync(0);
    VSync(0);
    ASSERT_FRAME("draw_disp_env_2", 0, 1.0f);

    // Now back buffer and front buffer are the same, display blue
    PutDispEnv(&db[0].disp);
    PutDrawEnv(&db[1].draw);
    ClearImage(&db[1].draw.clip, 0, 0, 0xFF);
    DrawSync(0);
    VSync(0);
    ASSERT_FRAME("draw_disp_env_3", 0, 1.0f);
}

ZTEST(gpu, clear_screen_draw_offset_bugfix) {
    DRAWENV draw;
    SetTile(&cdb->tile[0]);
    setRGB0(&cdb->tile[0], 255, 0, 0);
    setXY0(&cdb->tile[0], 0, 0);
    setWH(&cdb->tile[0], 128, 128);

    draw = cdb->draw;
    draw.ofs[0] = 128;
    draw.ofs[1] = 128;
    PutDrawEnv(&draw);
    PutDispEnv(&cdb->disp);

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].tile[0]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);

    ASSERT_FRAME("clear_screen_draw_offset_bugfix", 0, 1.0f);
}

// TODO: test is actually failing
ZTEST(gpu, load_move_image_priority) {
    TIM_IMAGE tim;
    RECT rectMoveNull = {16, 16, 64, 64};
    RECT rectBlit = {16, 16, 64, 64};
    RECT rectMoveImage = {16, 16, 64, 64};
    OpenTIM((u_long*)img_16bpp);
    ReadTIM(&tim);

    MoveImage(&rectMoveNull, 16, 80);

    SetTile(&cdb->tile[0]);
    setRGB0(&cdb->tile[0], 255, 0, 0);
    setXY0(&cdb->tile[0], 32, 32);
    setWH(&cdb->tile[0], 32, 32);
    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].tile[0]);

    LoadImage(&rectBlit, tim.paddr);

    DrawOTag(cdb->ot);

    MoveImage(&rectMoveImage, 80, 16);

    DrawSync(0);
    VSync(0);
    cdb->disp.disp.x = 0;
    cdb->disp.disp.y = 0;
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("load_move_image_priority", 0, 0.9825f);
}

ZTEST(gpu, flipped_xy) {
    u_short tpage, clut;
    if (LoadTim(img_uv_4bpp, &tpage, &clut)) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        setPolyGT4(&cdb->gt4[i]);
        SetSemiTrans(&cdb->gt4[i], 0);
        SetShadeTex(&cdb->gt4[i], 0);
        setUVWH(&cdb->gt4[i], 0, 0, 64, 64);
        cdb->gt4[i].tpage = tpage;
        cdb->gt4[i].clut = clut;
        AddPrim(cdb->ot, &cdb->gt4[i]);
    }
    setRGB0(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB1(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB2(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB3(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setXY4(&cdb->gt4[0], 0, 0, 64, 0, 0, 64, 64, 64);
    setRGB0(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB1(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB2(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB3(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setXY4(&cdb->gt4[1], 128, 0, 64, 0, 128, 64, 64, 64);
    setRGB0(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB1(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB2(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB3(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setXY4(&cdb->gt4[2], 0, 128, 64, 128, 0, 64, 64, 64);
    setRGB0(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB1(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB2(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB3(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setXY4(&cdb->gt4[3], 128, 128, 64, 128, 128, 64, 64, 64);

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("flipped_xy", 1, 1.0f);
}

ZTEST(gpu, flipped_uv) {
    u_short tpage, clut;
    if (LoadTim(img_uv_4bpp, &tpage, &clut)) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        setPolyGT4(&cdb->gt4[i]);
        SetSemiTrans(&cdb->gt4[i], 0);
        SetShadeTex(&cdb->gt4[i], 0);
        cdb->gt4[i].tpage = tpage;
        cdb->gt4[i].clut = clut;
        AddPrim(cdb->ot, &cdb->gt4[i]);
    }
    setXYWH(&cdb->gt4[0], 0, 0, 64, 64);
    setRGB0(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB1(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB2(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB3(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setUV4(&cdb->gt4[0], 0, 0, 64, 0, 0, 64, 64, 64);

    setXYWH(&cdb->gt4[1], 64, 0, 64, 64);
    setRGB0(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB1(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB2(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB3(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setUV4(&cdb->gt4[1], 64, 0, 0, 0, 64, 64, 0, 64);

    setXYWH(&cdb->gt4[2], 0, 64, 64, 64);
    setRGB0(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB1(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB2(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB3(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setUV4(&cdb->gt4[2], 0, 64, 64, 64, 0, 0, 64, 0);

    setXYWH(&cdb->gt4[3], 64, 64, 64, 64);
    setRGB0(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB1(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB2(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB3(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setUV4(&cdb->gt4[3], 64, 64, 0, 64, 64, 0, 0, 0);

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("flipped_uv", 1, 1.0f);
}

ZTEST(gpu, flipped_xy_uv) {
    u_short tpage, clut;
    if (LoadTim(img_uv_4bpp, &tpage, &clut)) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        setPolyGT4(&cdb->gt4[i]);
        SetSemiTrans(&cdb->gt4[i], 0);
        SetShadeTex(&cdb->gt4[i], 0);
        cdb->gt4[i].tpage = tpage;
        cdb->gt4[i].clut = clut;
        AddPrim(cdb->ot, &cdb->gt4[i]);
    }
    setXY4(&cdb->gt4[0], 0, 0, 64, 0, 0, 64, 64, 64);
    setRGB0(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB1(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB2(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setRGB3(&cdb->gt4[0], 0xFF, 0xFF, 0xFF);
    setUV4(&cdb->gt4[0], 0, 0, 64, 0, 0, 64, 64, 64);

    setXY4(&cdb->gt4[1], 128, 0, 64, 0, 128, 64, 64, 64);
    setRGB0(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB1(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB2(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setRGB3(&cdb->gt4[1], 0xFF, 0x00, 0x00);
    setUV4(&cdb->gt4[1], 64, 0, 0, 0, 64, 64, 0, 64);

    setXY4(&cdb->gt4[2], 0, 128, 64, 128, 0, 64, 64, 64);
    setRGB0(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB1(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB2(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setRGB3(&cdb->gt4[2], 0x00, 0xFF, 0x00);
    setUV4(&cdb->gt4[2], 0, 64, 64, 64, 0, 0, 64, 0);

    setXY4(&cdb->gt4[3], 128, 128, 64, 128, 128, 64, 64, 64);
    setRGB0(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB1(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB2(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setRGB3(&cdb->gt4[3], 0x00, 0x00, 0xFF);
    setUV4(&cdb->gt4[3], 64, 64, 0, 64, 64, 0, 0, 0);

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("flipped_xy_uv", 1, 1.0f);
}

static void DrawAlphaBlend(int flip_stp) {
    static u_short flipped[16];
    u_short tpage, clut;
    TIM_IMAGE tim;
    u_short* pal;
    if (OpenTIM((u_long*)img_4bpp)) {
        return;
    }
    if (!ReadTIM(&tim)) {
        return;
    }
    pal = (u_short*)tim.caddr;
    for (int i = 0; i < tim.crect->w * tim.crect->h; i++) {
        if (i == 2) // skip key color index
            continue;
        pal[i] |= 0x8000;
    }
    LoadImage(tim.prect, tim.paddr);
    LoadImage(tim.crect, tim.caddr);
    if (flip_stp) {
        StoreImage(tim.crect, (u_long*)flipped);
        DrawSync(0);
        for (int i = 0; i < 16; i++) {
            flipped[i] ^= 0x8000;
        }
        LoadImage(tim.crect, (u_long*)flipped);
    }
    tpage = GetTPage((int)tim.mode, 0, tim.prect->x, tim.prect->y);
    clut = GetClut(tim.crect->x, tim.crect->y);

    SetPolyF4Img(&db[0].ft4[0], 8, 8, 64, 64, 0, 0, tpage, clut, 0);
    SetPolyF4Img(&db[0].ft4[1], 88, 8, 64, 64, 0, 0, tpage | 0x20, clut, 0);
    SetPolyF4Img(&db[0].ft4[2], 168, 8, 64, 64, 0, 0, tpage | 0x40, clut, 0);
    SetPolyF4Img(&db[0].ft4[3], 8, 88, 64, 64, 0, 0, tpage | 0x60, clut, 1);
    SetPolyF4Img(&db[0].ft4[4], 88, 88, 64, 64, 0, 0, tpage, clut, 1);
    SetPolyF4Img(&db[0].ft4[5], 168, 88, 64, 64, 0, 0, tpage | 0x20, clut, 1);
    SetPolyF4Img(&db[0].ft4[6], 8, 168, 64, 64, 0, 0, tpage | 0x40, clut, 1);

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &db[0].ft4[0]);
    AddPrim(cdb->ot, &db[0].ft4[1]);
    AddPrim(cdb->ot, &db[0].ft4[2]);
    AddPrim(cdb->ot, &db[0].ft4[3]);
    AddPrim(cdb->ot, &db[0].ft4[4]);
    AddPrim(cdb->ot, &db[0].ft4[5]);
    AddPrim(cdb->ot, &db[0].ft4[6]);

    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
}

ZTEST(gpu, alpha_blend) {
    zskip_targets("pcsx-redux"); // differs from real hardware
    DrawAlphaBlend(0);
    ASSERT_FRAME("alpha_blend", 1, 1.0f);
}

ZTEST(gpu, alpha_blend_stp_flipped) {
    zskip_targets("pcsx-redux"); // differs from real hardware
    DrawAlphaBlend(1);
    ASSERT_FRAME("alpha_blend_stp_flipped", 1, 1.0f);
}

ZTEST(gpu, s11_coord_truncation) {
    ClearImage(&cdb->draw.clip, 60, 120, 120);
    DrawSync(0);

    SetTile(&cdb->tile[0]);
    setRGB0(&cdb->tile[0], 255, 0, 0);
    setXY0(&cdb->tile[0], (short)0x8014, (short)0x8014);
    setWH(&cdb->tile[0], 32, 32);

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &cdb->tile[0]);

    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);
    ASSERT_FRAME("s11_coord_truncation", 0, 1.0f);
}

ZTEST(gpu, untextured_transp_poly_take_abr_from_drawenv) {
    const int16_t bx[4] = {8, 88, 8, 88};
    const int16_t by[4] = {8, 8, 88, 88};
    ClearImage(&cdb->draw.clip, 0x60, 0x60, 0x60);
    DrawSync(0);

    for (int i = 0; i < 4; i++) {
        POLY_F4 poly;
        unsigned* words;
        Psyz_GpuWriteGP0(_get_mode(1, 0, getTPage(0, i, 0, 0)));

        SetPolyF4(&poly);
        SetSemiTrans(&poly, 1);
        setRGB0(&poly, 0x80, 0x40, 0xC0);
        setXYWH(&poly, bx[i], by[i], 64, 64);

        words = (unsigned*)&poly + sizeof(OT_TYPE) / sizeof(unsigned);
        Psyz_GpuWriteGP0(*words++);
        Psyz_GpuWriteGP0(*words++);
        Psyz_GpuWriteGP0(*words++);
        Psyz_GpuWriteGP0(*words++);
        Psyz_GpuWriteGP0(*words++);
    }
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("abr_untextured", 0, 1.0f);
}

ZTEST(gpu, uv_minification) {
    u_short tpage, clut;
    static const struct {
        short x, y, w, h;
    } cases[] = {// 64 texels into 32 pixels: 2x minified
                 {8, 8, 32, 32},
                 // 64 texels into 16 pixels: 4x minified
                 {88, 8, 16, 16},
                 // non-power-of-two step
                 {8, 88, 21, 21},
                 // 1:1 for reference
                 {88, 88, 64, 64}};
    zskip_targets("ppsspp");
    if (LoadTim(img_uv_4bpp, &tpage, &clut)) {
        return;
    }

    ClearOTag(cdb->ot, OTSIZE);
    for (int i = 0; i < LEN(cases); i++) {
        SetPolyFT4(&cdb->ft4[i]);
        setXYWH(&cdb->ft4[i], cases[i].x, cases[i].y, cases[i].w, cases[i].h);
        setRGB0(&cdb->ft4[i], 128, 128, 128);
        setUVWH(&cdb->ft4[i], 0, 0, 64, 64);
        setSemiTrans(&cdb->ft4[i], 0);
        cdb->ft4[i].tpage = tpage;
        cdb->ft4[i].clut = clut;
        AddPrim(cdb->ot, &cdb->ft4[i]);
    }

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    // PSP has its own golden: a slightly different minification algorithm
    ASSERT_FRAME("uv_minification", 0, 0.9995f);
}

ZTEST(gpu, texture_window_tiling) {
    u_short tpage, clut;
    static const struct {
        short x, y;
        RECT win;
    } cases[] = {
        // full page: window disabled
        {8, 8, {0, 0, 256, 256}},
        // window matches the quad: still one copy
        {88, 8, {0, 0, 64, 64}},
        // 32x32 window: 2x2 repeats
        {8, 88, {0, 0, 32, 32}},
        // 16x16 window: 4x4 repeats
        {88, 88, {0, 0, 16, 16}},
    };
    zskip_targets("psp");
    if (LoadTim(img_uv_4bpp, &tpage, &clut)) {
        return;
    }

    ClearOTag(cdb->ot, OTSIZE);
    for (int i = 0; i < LEN(cases); i++) {
        RECT win;
        SetPolyFT4(&cdb->ft4[i]);
        setXYWH(&cdb->ft4[i], cases[i].x, cases[i].y, 64, 64);
        setRGB0(&cdb->ft4[i], 128, 128, 128);
        setUVWH(&cdb->ft4[i], 0, 0, 64, 64);
        setSemiTrans(&cdb->ft4[i], 0);
        cdb->ft4[i].tpage = tpage;
        cdb->ft4[i].clut = clut;

        win = cases[i].win;
        SetTexWindow(&cdb->twin[i], &win);

        AddPrim(cdb->ot, &cdb->ft4[i]);
        AddPrim(cdb->ot, &cdb->twin[i]);
    }

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("texture_window_tiling", 0, 1.0f);
}

ZTEST(gpu, texture_window_offset) {
    // Same 32x32 window size in every quadrant, moved around the page. The
    // offset bits replace the masked-off UV bits, so each quadrant tiles a
    // different 32x32 patch of the texture.
    u_short tpage, clut;
    static const struct {
        short x, y;
        RECT win;
    } cases[] = {
        {8, 8, {0, 0, 32, 32}},
        {88, 8, {32, 0, 32, 32}},
        {8, 88, {0, 32, 32, 32}},
        {88, 88, {32, 32, 32, 32}},
    };
    zskip_targets("psp");
    if (LoadTim(img_uv_4bpp, &tpage, &clut)) {
        return;
    }

    ClearOTag(cdb->ot, OTSIZE);
    for (int i = 0; i < LEN(cases); i++) {
        RECT win;
        SetPolyFT4(&cdb->ft4[i]);
        setXYWH(&cdb->ft4[i], cases[i].x, cases[i].y, 64, 64);
        setRGB0(&cdb->ft4[i], 128, 128, 128);
        setUVWH(&cdb->ft4[i], 0, 0, 64, 64);
        setSemiTrans(&cdb->ft4[i], 0);
        cdb->ft4[i].tpage = tpage;
        cdb->ft4[i].clut = clut;

        win = cases[i].win;
        SetTexWindow(&cdb->twin[i], &win);

        AddPrim(cdb->ot, &cdb->ft4[i]);
        AddPrim(cdb->ot, &cdb->twin[i]);
    }

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("texture_window_offset", 0, 1.0f);
}

ZTEST(gpu, texture_window_non_square) {
    u_short tpage, clut;
    static const struct {
        short x, y;
        u_char u, v;
        RECT win;
    } cases[] = {
        // wide and short
        {8, 8, 0, 0, {0, 0, 64, 16}},
        // narrow and tall
        {88, 8, 0, 0, {0, 0, 16, 64}},
        // non-zero starting UV
        {8, 88, 96, 96, {0, 0, 32, 32}},
        // window offset not aligned to window size
        {88, 88, 0, 0, {8, 8, 32, 32}},
    };
    zskip_targets("psp");
    if (LoadTim(img_uv_4bpp, &tpage, &clut)) {
        return;
    }

    ClearOTag(cdb->ot, OTSIZE);
    for (int i = 0; i < LEN(cases); i++) {
        RECT win;
        SetPolyFT4(&cdb->ft4[i]);
        setXYWH(&cdb->ft4[i], cases[i].x, cases[i].y, 64, 64);
        setRGB0(&cdb->ft4[i], 128, 128, 128);
        setUVWH(&cdb->ft4[i], cases[i].u, cases[i].v, 64, 64);
        setSemiTrans(&cdb->ft4[i], 0);
        cdb->ft4[i].tpage = tpage;
        cdb->ft4[i].clut = clut;

        win = cases[i].win;
        SetTexWindow(&cdb->twin[i], &win);

        AddPrim(cdb->ot, &cdb->ft4[i]);
        AddPrim(cdb->ot, &cdb->twin[i]);
    }

    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawOTag(cdb->ot);
    DrawSync(0);
    VSync(0);
    PutDispEnv(&cdb->disp);

    ASSERT_FRAME("texture_window_non_square", 0, 1.0f);
}

ZTEST(gpu, marge_prim) {
    SetPolyF4(&cdb->f4[0]);
    setXYWH(&cdb->f4[0], 16, 16, 64, 64);
    setRGB0(&cdb->f4[0], 0, 0, 255);
    setSemiTrans(&cdb->f4[0], 0);

    SetPolyF4(&cdb->f4[1]);
    setXYWH(&cdb->f4[1], 32, 32, 32, 32);
    setRGB0(&cdb->f4[1], 0, 255, 0);
    setSemiTrans(&cdb->f4[1], 0);

    MargePrim(&cdb->f4[0], &cdb->f4[1]);

    SetPolyF4(&cdb->f4[2]);
    setXYWH(&cdb->f4[2], 40, 40, 16, 16);
    setRGB0(&cdb->f4[2], 255, 0, 0);
    setSemiTrans(&cdb->f4[2], 0);

    ClearOTag(cdb->ot, OTSIZE);
    AddPrim(cdb->ot, &cdb->f4[2]);
    AddPrim(cdb->ot, &cdb->f4[0]);
    Present("marge_prim");
}

enum {
    DR = 47,
    DG = 123,
    DB = 239,
    DBG = 91,
    TEX_X = 512,
    TEX_Y = 256,
    TEX_SIZE = 64,
};

static u_short MakeFlatTPage(void) {
    RECT tex = {TEX_X, TEX_Y, TEX_SIZE, TEX_SIZE};
    ClearImage(&tex, 255, 255, 255);
    DrawSync(0);
    return GetTPage(2, 0, TEX_X, TEX_Y);
}

static u_short MakeFlatTPageSemiTrans(void) {
    static u_short texels[TEX_SIZE * TEX_SIZE];
    RECT tex = {TEX_X, TEX_Y, TEX_SIZE, TEX_SIZE};
    for (int i = 0; i < TEX_SIZE * TEX_SIZE; i++) {
        texels[i] = 0xFFFF; // STP | 31/31/31
    }
    LoadImage(&tex, (u_long*)texels);
    DrawSync(0);
    return GetTPage(2, 0, TEX_X, TEX_Y);
}

ZTEST_SETUP(dither) {
    gpu_setup();
    Psyz_VideoSetDitheringMode(PSYZ_DITHER_AUTO);
    cdb->draw.dtd = 1;
    PutDrawEnv(&cdb->draw);
    ClearOTag(cdb->ot, OTSIZE);
    ClearImage(&cdb->draw.clip, 0, 0, 0);
}

ZTEST_TEARDOWN(dither) { ResetGraph(0); }

ZTEST(dither, dithering_drawenv_disabled) {
    cdb->draw.dtd = 0;
    PutDrawEnv(&cdb->draw);

    SetPolyG4(&cdb->g4[0]);
    setXYWH(&cdb->g4[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&cdb->g4[0], DR, DG, DB);
    setRGB1(&cdb->g4[0], DR, DG, DB);
    setRGB2(&cdb->g4[0], DR, DG, DB);
    setRGB3(&cdb->g4[0], DR, DG, DB);
    AddPrim(cdb->ot, &cdb->g4[0]);
    Present("dithering_drawenv_disabled");
}

ZTEST(dither, dithering_gouraud_on) {
    SetPolyG4(&cdb->g4[0]);
    setXYWH(&cdb->g4[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&cdb->g4[0], DR, DG, DB);
    setRGB1(&cdb->g4[0], DR, DG, DB);
    setRGB2(&cdb->g4[0], DR, DG, DB);
    setRGB3(&cdb->g4[0], DR, DG, DB);
    AddPrim(cdb->ot, &cdb->g4[0]);
    Present("dithering_gouraud_on");
}

ZTEST(dither, dithering_flat_off) {
    SetPolyF4(&cdb->f4[0]);
    setXYWH(&cdb->f4[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&cdb->f4[0], DR, DG, DB);
    AddPrim(cdb->ot, &cdb->f4[0]);
    Present("dithering_flat_off");
}

ZTEST(dither, dithering_flat_blending_on) {
    zskip_targets("pcsx-redux"); // differs from real hardware
    // PSP has its own golden: the dithering matrix is fixed in the GPU
    // pipeline, it can't be changed to reflect the exact identical look on PS1.
    u_short tpage = MakeFlatTPage();
    SetPolyFT4(&cdb->ft4[0]);
    setXYWH(&cdb->ft4[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&cdb->ft4[0], DR, DG, DB);
    setUVWH(&cdb->ft4[0], 0, 0, TEX_SIZE - 1, TEX_SIZE - 1);
    setSemiTrans(&cdb->ft4[0], 0);
    cdb->ft4[0].tpage = tpage;
    cdb->ft4[0].clut = 0;
    AddPrim(cdb->ot, &cdb->ft4[0]);
    Present("dithering_flat_blending_on");
}

ZTEST(dither, dithering_lines_on) {
    zskip_targets("pcsx-redux"); // differs from real hardware
    static LINE_F2 flat[SCREEN_HEIGHT / 2];
    static LINE_G2 grad[SCREEN_HEIGHT / 2];
    int nf = 0, ng = 0;

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        if ((y / 8) & 1) {
            LINE_G2* l = &grad[ng++];
            SetLineG2(l);
            setXY2(l, 0, y, SCREEN_WIDTH, y);
            setRGB0(l, DR, DG, DB);
            setRGB1(l, DR, DG, DB);
            AddPrim(cdb->ot, l);
        } else {
            LINE_F2* l = &flat[nf++];
            SetLineF2(l);
            setXY2(l, 0, y, SCREEN_WIDTH, y);
            setRGB0(l, DR, DG, DB);
            AddPrim(cdb->ot, l);
        }
    }
    Present("dithering_lines_on");
}

ZTEST(dither, dithering_tile_off) {
    SetTile(&cdb->tile[0]);
    setXY0(&cdb->tile[0], 0, 0);
    setWH(&cdb->tile[0], SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&cdb->tile[0], DR, DG, DB);
    AddPrim(cdb->ot, &cdb->tile[0]);
    Present("dithering_tile_off");
}

ZTEST(dither, dithering_tile_blending_off) {
    RECT bg = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    ClearImage(&bg, DBG, DBG, DBG);
    DrawSync(0);

    SetTile(&cdb->tile[0]);
    SetSemiTrans(&cdb->tile[0], 1);
    setXY0(&cdb->tile[0], 0, 0);
    setWH(&cdb->tile[0], SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&cdb->tile[0], DR, DG, DB);
    AddPrim(cdb->ot, &cdb->tile[0]);

    SetDrawMode(&cdb->drmode[0], 0, 1, (int)getTPage(0, 0, 0, 0), NULL);
    AddPrim(cdb->ot, &cdb->drmode[0]);

    Present("dithering_tile_blending_off");
}

ZTEST(dither, dithering_sprite_off) {
    enum {
        COLS = (SCREEN_WIDTH + TEX_SIZE - 1) / TEX_SIZE,
        ROWS = (SCREEN_HEIGHT + TEX_SIZE - 1) / TEX_SIZE,
    };
    SPRT spr[COLS * ROWS];
    cdb->draw.tpage = MakeFlatTPage();
    PutDrawEnv(&cdb->draw);

    for (int i = COLS * ROWS - 1; i >= 0; i--) {
        SPRT* s = &spr[i];
        SetSprt(s);
        SetSemiTrans(s, 0);
        SetShadeTex(s, 0); // modulated
        setXY0(s, (i % COLS) * TEX_SIZE, (i / COLS) * TEX_SIZE);
        setWH(s, TEX_SIZE, TEX_SIZE);
        setUV0(s, 0, 0);
        setRGB0(s, DR, DG, DB);
        s->clut = 0;
        AddPrim(cdb->ot, s);
    }
    Present("dithering_sprite_off");
}

ZTEST(dither, dithering_sprite_blending_off) {
    zskip_targets("pcsx-redux"); // differs from real hardware
    enum {
        COLS = (SCREEN_WIDTH + TEX_SIZE - 1) / TEX_SIZE,
        ROWS = (SCREEN_HEIGHT + TEX_SIZE - 1) / TEX_SIZE,
    };
    RECT bg = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    SPRT spr[COLS * ROWS];
    cdb->draw.tpage = MakeFlatTPageSemiTrans() | (0 << 5);
    PutDrawEnv(&cdb->draw);

    ClearImage(&bg, DBG, DBG, DBG);
    DrawSync(0);

    for (int i = COLS * ROWS - 1; i >= 0; i--) {
        SPRT* s = &spr[i];
        SetSprt(s);
        SetSemiTrans(s, 1); // blended
        SetShadeTex(s, 0);  // modulated
        setXY0(s, (i % COLS) * TEX_SIZE, (i / COLS) * TEX_SIZE);
        setWH(s, TEX_SIZE, TEX_SIZE);
        setUV0(s, 0, 0);
        setRGB0(s, DR, DG, DB);
        s->clut = 0;
        AddPrim(cdb->ot, s);
    }
    Present("dithering_sprite_blending_off");
}

ZTEST(dither, dithering_pattern_alignment) {
    enum {
        SIZE = 5, // 5x5 tile
        BAND_H = 60,
        GRID_COLS = (SCREEN_WIDTH + SIZE - 1) / SIZE,
        GRID_ROWS = (BAND_H + SIZE - 1) / SIZE,
    };
    // static: primitive arrays this size do not fit on the PS1 stack
    static POLY_G4 grid[GRID_COLS * GRID_ROWS];

    SetPolyG4(&cdb->g4[0]);
    setXYWH(&cdb->g4[0], 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    setRGB0(&cdb->g4[0], DR, DG, DB);
    setRGB1(&cdb->g4[0], DR, DG, DB);
    setRGB2(&cdb->g4[0], DR, DG, DB);
    setRGB3(&cdb->g4[0], DR, DG, DB);

    for (int i = GRID_COLS * GRID_ROWS - 1; i >= 0; i--) {
        const int gx = (i % GRID_COLS) * SIZE;
        const int gy = (i / GRID_COLS) * SIZE;
        // clip the last row/column so the grid stays inside the band
        const int gw = (gx + SIZE > SCREEN_WIDTH) ? SCREEN_WIDTH - gx : SIZE;
        const int gh = (gy + SIZE > BAND_H) ? BAND_H - gy : SIZE;
        SetPolyG4(&grid[i]);
        setXYWH(&grid[i], gx, gy, gw, gh);
        setRGB0(&grid[i], DR, DG, DB);
        setRGB1(&grid[i], DR, DG, DB);
        setRGB2(&grid[i], DR, DG, DB);
        setRGB3(&grid[i], DR, DG, DB);
        AddPrim(cdb->ot, &grid[i]);
    }
    AddPrim(cdb->ot, &cdb->g4[0]);
    Present("dithering_pattern_alignment");
}

enum {
    GRID_OPCODE = 0x04,
    DISP_W = 320,
    DISP_H = 240,
    PSX_W = 256,
    PACK_W = 40,
    CELL = 8,
    BAND_H = 16,
    NATIVE_Y = 48,
    PSX_Y = 96,
    GUIDE_Y0 = 40,
    GUIDE_Y1 = 120,
};

typedef struct {
    O_TAG;
    u_long code[1];
} DR_GRID;

static TILE tiles[96];
static DR_GRID grids[8];
static int tile_count;
static int grid_count;
static void* tail;
static int handler_calls;

static int HandleGrid(const u_long* words, int available, void* userdata) {
    (void)userdata;
    if (available < 1) {
        return 0;
    }
    handler_calls++;
    if (Psyz_GpuSetHorizontalGrid((unsigned int)(words[0] & 0xFFFF), DISP_W) <
        0) {
        return 0;
    }
    return 1;
}

static void Append(void* prim) {
    if (tail) {
        setaddr(tail, prim);
    } else {
        setaddr(cdb->ot, prim);
    }
    tail = prim;
    termPrim(prim);
}

static void AddGrid(int source_width) {
    DR_GRID* grid = &grids[grid_count++];
    setlen(grid, 1);
    grid->code[0] =
        ((u_long)GRID_OPCODE << 24) | ((u_long)source_width & 0xFFFF);
    Append(grid);
}

static void AddRect(int x, int y, int w, int h, int r, int g, int b) {
    TILE* tile = &tiles[tile_count++];
    SetTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, r, g, b);
    Append(tile);
}

static void DrawBands(void) {
    static const unsigned char native[5][3] = {
        {0x40, 0x50, 0xE0},
        {0x40, 0xB0, 0xE0},
        {0x40, 0xD0, 0x80},
        {0x80, 0xD0, 0x40},
        {0xD0, 0xC0, 0x40}};
    static const unsigned char psx[4][3] = {
        {0xE0, 0x80, 0x40},
        {0xD0, 0x40, 0x60},
        {0xB0, 0x40, 0xD0},
        {0x70, 0x40, 0xD0}};
    int i;

    AddGrid(DISP_W);
    for (i = 0; i < DISP_W / CELL; i++) {
        const unsigned char* c = native[i % 5];
        AddRect(i * CELL, NATIVE_Y, CELL, BAND_H, c[0], c[1], c[2]);
    }

    AddGrid(PSX_W);
    for (i = 0; i < PSX_W / CELL; i++) {
        const unsigned char* c = psx[i % 4];
        AddRect(i * CELL, PSX_Y, CELL, BAND_H, c[0], c[1], c[2]);
    }

    AddGrid(DISP_W);
    for (i = 0; i < DISP_W; i += PACK_W) {
        AddRect(i, GUIDE_Y0, 1, GUIDE_Y1 - GUIDE_Y0, 255, 255, 255);
    }
}

ZTEST_SETUP(horizontal_grid) {
    zskip_targets("psp;ps1");
    gpu_setup();
    SetDefDrawEnv(&cdb->draw, 0, 0, DISP_W, DISP_H);
    SetDefDispEnv(&cdb->disp, 0, 0, DISP_W, DISP_H);
    cdb->draw.dtd = 0;
    PutDrawEnv(&cdb->draw);
    PutDispEnv(&cdb->disp);
    ClearOTag(cdb->ot, OTSIZE);
    ClearImage(&cdb->draw.clip, 0, 0, 0);
    DrawSync(0);
    tile_count = 0;
    grid_count = 0;
    tail = NULL;
    handler_calls = 0;
    zassert_s32_eq(
        0, Psyz_GpuRegisterCommandHandler(GRID_OPCODE, HandleGrid, NULL));
}

ZTEST_TEARDOWN(horizontal_grid) {
    Psyz_GpuRegisterCommandHandler(GRID_OPCODE, NULL, NULL);
    Psyz_GpuSetHorizontalGrid(1, 1);
    Psyz_VideoSetInternalResolution(1);
    gpu_teardown();
}

ZTEST(horizontal_grid, horizontal_grid) {
    DrawBands();
    Present("horizontal_grid");
    zexpect_s32_eq(3, handler_calls);
}

// 2x and 4x reproduce the 1x golden
ZTEST(horizontal_grid, horizontal_grid_internal_res_2) {
    zassert_s32_eq(0, Psyz_VideoSetInternalResolution(2));
    DrawBands();
    Present("horizontal_grid");
}

ZTEST(horizontal_grid, horizontal_grid_internal_res_4) {
    zassert_s32_eq(0, Psyz_VideoSetInternalResolution(4));
    DrawBands();
    Present("horizontal_grid");
}

ZTEST(horizontal_grid, horizontal_grid_invalid_args) {
    zexpect_s32_eq(-1, Psyz_GpuSetHorizontalGrid(0, DISP_W));
    zexpect_s32_eq(-1, Psyz_GpuSetHorizontalGrid(DISP_W, 0));
    zexpect_s32_eq(0, Psyz_GpuSetHorizontalGrid(PSX_W, DISP_W));
    zexpect_s32_eq(0, Psyz_GpuSetHorizontalGrid(1, 1));
    zexpect_s32_eq(-1, Psyz_GpuRegisterCommandHandler(0x100, HandleGrid, NULL));
}

static volatile int vsync_callback_count;
static void CountVSync(void) { vsync_callback_count++; }

ZTEST(gpu, vsync_callback_runs_every_frame) {
    vsync_callback_count = 0;
    VSyncCallback(CountVSync);
    VSync(0);
    VSync(0);
    VSync(0);
    VSyncCallback(NULL);
    zexpect_s32_ge(2, vsync_callback_count);
    vsync_callback_count = 0;
    VSync(0);
    zexpect_s32_eq(0, vsync_callback_count);
}

static volatile int vsync_order[16];
static volatile int vsync_order_len;
static void RecordCh0(void) {
    if (vsync_order_len < 16) {
        vsync_order[vsync_order_len++] = 0;
    }
}
static void RecordCh3(void) {
    if (vsync_order_len < 16) {
        vsync_order[vsync_order_len++] = 3;
    }
}
static void RecordCh7(void) {
    if (vsync_order_len < 16) {
        vsync_order[vsync_order_len++] = 7;
    }
}

ZTEST(gpu, vsync_callback_uses_channel_4) {
    zexpect_s32_eq(0, VSyncCallback(CountVSync));
    zexpect_s32_eq((int)(intptr_t)CountVSync, VSyncCallbacks(4, NULL));
    zexpect_s32_eq(0, VSyncCallback(NULL));
}

ZTEST(gpu, vsync_callbacks_returns_previous) {
    zexpect_s32_eq(0, VSyncCallbacks(2, RecordCh0));
    zexpect_s32_eq((int)(intptr_t)RecordCh0, VSyncCallbacks(2, RecordCh3));
    zexpect_s32_eq((int)(intptr_t)RecordCh3, VSyncCallbacks(2, NULL));
    zexpect_s32_eq(0, VSyncCallbacks(2, NULL));
}

ZTEST(gpu, vsync_callbacks_run_in_channel_order) {
    VSyncCallbacks(7, RecordCh7);
    VSyncCallbacks(0, RecordCh0);
    VSyncCallbacks(3, RecordCh3);
    VSync(0);
    vsync_order_len = 0;
    VSync(0);
    VSyncCallbacks(7, NULL);
    VSyncCallbacks(0, NULL);
    VSyncCallbacks(3, NULL);
    zexpect_s32_ge(3, vsync_order_len);
    zexpect_s32_eq(0, vsync_order[0]);
    zexpect_s32_eq(3, vsync_order[1]);
    zexpect_s32_eq(7, vsync_order[2]);
}

/* Golden 16-bit GPU output from an isolated PS1 software reference. The
 * command producer advances GPU command ticks between writes; no game code
 * or CPU instruction is substituted. Covers UV/RGB sampling, signed 12-bit
 * gradients, leftmost-vertex ties, winding, dithering, raw/modulated textures
 * and subtractive transparency. */
ZTEST(gpu, ps1_polygon_attribute_gradients) {
    static const u32 expected[144] = {
        0x67e412fau, 0x67e412fau, 0x67e412fau, 0x67e412fau, 0x67e412fau,
        0x67e412fau, 0x79cb5bdeu, 0x79cb5bdeu, 0x79cb5bdeu, 0x79cb5bdeu,
        0x79cb5bdeu, 0x79cb5bdeu, 0xa131236bu, 0xa131236bu, 0xa131236bu,
        0xa131236bu, 0xa131236bu, 0xa131236bu, 0x79ca2affu, 0x79ca2affu,
        0x79ca2affu, 0x79ca2affu, 0x79ca2affu, 0x79ca2affu, 0xdb6f37ecu,
        0xdb6f37ecu, 0xdb6f37ecu, 0xdb6f37ecu, 0xdb6f37ecu, 0xdb6f37ecu,
        0x9c4010a5u, 0x9c4010a5u, 0x9c4010a5u, 0x9c4010a5u, 0x9c4010a5u,
        0x9c4010a5u, 0x0d2abcf8u, 0x0d2abcf8u, 0x0d2abcf8u, 0x0d2abcf8u,
        0x0d2abcf8u, 0x0d2abcf8u, 0xfd037c01u, 0xfd037c01u, 0xfd037c01u,
        0xfd037c01u, 0xfd037c01u, 0xfd037c01u, 0xf1fc5688u, 0xcf487513u,
        0xcf487513u, 0xf1fc5688u, 0xf1fc5688u, 0xcf487513u, 0x80476901u,
        0x5ed641bau, 0x5ed641bau, 0x80476901u, 0x80476901u, 0x5ed641bau,
        0xe35417e0u, 0x6544e4b8u, 0x6544e4b8u, 0xe35417e0u, 0xe35417e0u,
        0x6544e4b8u, 0x64ebb6ffu, 0x7f8974dcu, 0x7f8974dcu, 0x64ebb6ffu,
        0x64ebb6ffu, 0x7f8974dcu, 0x63191668u, 0x63191668u, 0x63191668u,
        0x63191668u, 0x63191668u, 0x63191668u, 0x79cb5bdeu, 0x79cb5bdeu,
        0x79cb5bdeu, 0x79cb5bdeu, 0x79cb5bdeu, 0x79cb5bdeu, 0x53a12a50u,
        0x53a12a50u, 0x53a12a50u, 0x53a12a50u, 0x53a12a50u, 0x53a12a50u,
        0x79ca2affu, 0x79ca2affu, 0x79ca2affu, 0x79ca2affu, 0x79ca2affu,
        0x79ca2affu, 0x75424c70u, 0x75424c70u, 0x75424c70u, 0x75424c70u,
        0x75424c70u, 0x75424c70u, 0x9c4010a5u, 0x9c4010a5u, 0x9c4010a5u,
        0x9c4010a5u, 0x9c4010a5u, 0x9c4010a5u, 0xa9b7bcfbu, 0xa9b7bcfbu,
        0xa9b7bcfbu, 0xa9b7bcfbu, 0xa9b7bcfbu, 0xa9b7bcfbu, 0xadd02e53u,
        0xadd02e53u, 0xadd02e53u, 0xadd02e53u, 0xadd02e53u, 0xadd02e53u,
        0x57665d54u, 0x8a43137bu, 0x8a43137bu, 0x57665d54u, 0x57665d54u,
        0x8a43137bu, 0x80476901u, 0x5ed641bau, 0x5ed641bau, 0x80476901u,
        0x80476901u, 0x5ed641bau, 0x2dccb2b4u, 0x50cb89b7u, 0x50cb89b7u,
        0x2dccb2b4u, 0x2dccb2b4u, 0x50cb89b7u, 0xf6cec02bu, 0xc84b9688u,
        0xc84b9688u, 0xf6cec02bu, 0xf6cec02bu, 0xc84b9688u,
    };
    typedef struct {
        short x, y;
        u_char u, v, r, g, b;
    } AttributeVertex;
    static const AttributeVertex shapes[3][3] = {
        {{5, 2, 86, 19, 58, 58, 58},
         {5, 5, 51, 21, 122, 122, 122},
         {3, 4, 55, 16, 107, 107, 107}},
        {{2, 3, 5, 2, 17, 220, 75},
         {54, 11, 47, 17, 221, 23, 184},
         {13, 42, 83, 49, 119, 90, 240}},
        {{3, 2, 70, 10, 22, 188, 53},
         {3, 40, 18, 24, 201, 40, 225},
         {52, 15, 110, 42, 101, 167, 91}}};
    static const unsigned order[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    _Alignas(8) u16 texture[64 * 64], palette[16], pixels[64 * 48];
    gpu_setup();
    Psyz_VideoSetDitheringMode(PSYZ_DITHER_AUTO);
    for (unsigned y = 0; y < 64; y++)
        for (unsigned x = 0; x < 64; x++) {
            u16 packed = 0;
            for (unsigned k = 0; k < 4; k++) {
                unsigned u = x * 4 + k;
                packed |= ((u * 3 + y * 5 + (u >> 2)) & 15) << (k * 4);
            }
            texture[y * 64 + x] = packed;
        }
    for (unsigned i = 0; i < 16; i++)
        palette[i] = (i + 1) | (((i * 5 + 3) & 31) << 5) |
                     (((i * 9 + 7) & 31) << 10) | 0x8000;
    RECT texture_rect = {960, 448, 64, 64}, palette_rect = {0, 480, 16, 1};
    LoadImage(&texture_rect, (u_long*)texture);
    LoadImage(&palette_rect, (u_long*)palette);
    DrawSync(0);
    unsigned scales = 1;
#if defined(__psyz) && defined(PSYZ_HAS_INTERNAL_RESOLUTION_SCALE)
    const char* headless = getenv("PSYZ_HEADLESS");
    if (!headless || strcmp(headless, "1"))
        scales = PSYZ_INTERNAL_RES_MAX;
#endif
    for (unsigned scale = 1; scale <= scales; ++scale) {
        zassert_s32_eq(0, Psyz_VideoSetInternalResolution(scale));
        zassert_s32_eq(scale, Psyz_VideoGetInternalResolution());
        unsigned test = 0;
        for (unsigned dither = 0; dither < 2; dither++)
            for (unsigned shape = 0; shape < 3; shape++)
                for (unsigned kind = 0; kind < 4; kind++)
                    for (unsigned permutation = 0; permutation < 6;
                         permutation++, test++) {
                        int x = (test % 16) * 64, y = (test / 16) * 48;
                        u16 tpage = 31 | (kind == 3 ? 64 : 0);
                        AttributeVertex a =
                            shapes[shape][order[permutation][0]];
                        AttributeVertex b =
                            shapes[shape][order[permutation][1]];
                        AttributeVertex c =
                            shapes[shape][order[permutation][2]];
                        DRAWENV env;
                        SetDefDrawEnv(&env, x, y, 64, 48);
                        env.dtd = dither;
                        env.dfe = 1;
                        env.tpage = tpage;
                        env.isbg = 1;
                        env.r0 = 40;
                        env.g0 = 48;
                        env.b0 = 56;
                        PutDrawEnv(&env);
                        if (kind >= 2) {
                            POLY_GT3 polygon = {0};
                            setPolyGT3(&polygon);
                            setSemiTrans(&polygon, kind == 3);
                            setXY3(&polygon, a.x, a.y, b.x, b.y, c.x, c.y);
                            setUV3(&polygon, a.u, a.v + 192, b.u, b.v + 192,
                                   c.u, c.v + 192);
                            setRGB0(&polygon, a.r, a.g, a.b);
                            setRGB1(&polygon, b.r, b.g, b.b);
                            setRGB2(&polygon, c.r, c.g, c.b);
                            polygon.clut = GetClut(0, 480);
                            polygon.tpage = tpage;
                            DrawPrim(&polygon);
                            DrawSync(0);
                        } else {
                            POLY_FT3 polygon = {0};
                            setPolyFT3(&polygon);
                            setShadeTex(&polygon, kind == 1);
                            setXY3(&polygon, a.x, a.y, b.x, b.y, c.x, c.y);
                            setUV3(&polygon, a.u, a.v + 192, b.u, b.v + 192,
                                   c.u, c.v + 192);
                            setRGB0(&polygon, 127, 113, 85);
                            polygon.clut = GetClut(0, 480);
                            polygon.tpage = tpage;
                            DrawPrim(&polygon);
                            DrawSync(0);
                        }
                        RECT sample = {x, y, 64, 48};
                        StoreImage(&sample, (u_long*)pixels);
                        DrawSync(0);
                        u32 hash = 2166136261u;
                        for (unsigned i = 0; i < 64 * 48; i++) {
                            hash = (hash ^ (pixels[i] & 255)) * 16777619u;
                            hash = (hash ^ (pixels[i] >> 8)) * 16777619u;
                        }
                        zexpect_u32_eq(expected[test], hash);
                    }
        zexpect_u32_eq(144, test);
    }
    zassert_s32_eq(0, Psyz_VideoSetInternalResolution(1));
}
