#ifndef RASTERIZER_H
#define RASTERIZER_H

#define RECT PS1_RECT
#include "../draw.h"
#undef RECT

typedef struct {
    void (*reset)(void);
    void (*texpage_mode)(ParamDrawTexpageMode* mode);
    void (*texture_window)(
        unsigned int mx, unsigned int my, unsigned int ox, unsigned int oy);
    void (*area_start)(int x, int y);
    void (*area_end)(int x, int y);
    void (*offset)(int x, int y);
    int (*horizontal_grid)(unsigned int source, unsigned int target);
    void (*mask)(int set, int preserve);
    void (*clear_cache)(void);
    void (*clear_image)(PS1_RECT* rect, u_char r, u_char g, u_char b);
    void (*load_image)(PS1_RECT* rect, u_long* data);
    void (*store_image)(PS1_RECT* rect, u_long* data);
    void (*move_image)(PS1_RECT* rect, unsigned int x, unsigned int y);
    void (*reset_buffer)(void);
    void (*flush_buffer)(void);
    int (*push_prim)(u_long* words, int count);
    int (*exeque_sync)(void);
} DrawBackend;

extern const DrawBackend software_rasterizer;
void SoftwareRasterizer_Reset(void);
const u16* SoftwareRasterizer_GetVram(void);
void SoftwareRasterizer_SetVram(const u16* pixels);
void SoftwareRasterizer_InvalidateMaterial(void);
void SoftwareRasterizer_MaterialCoverage(unsigned long long *covered,unsigned long long *total);
void SoftwareRasterizer_SetMaterialSampler(
    int (*sample)(unsigned,unsigned,uint16_t,uint16_t[4]));

#endif

void SoftwareRasterizer_Begin2x(int,int,int,int,int);
unsigned char *SoftwareRasterizer_Alloc2x(int,int,int,int,int);
void SoftwareRasterizer_FreeGeometry(void);
void SoftwareRasterizer_ReleaseGeometryFrame(void *pixels);
int SoftwareRasterizer_GeometryReason(void);

void SoftwareRasterizer_Reject2x(void);

/* CPU wide pages never alias canonical VRAM or the ordinary geometry epoch. */
void SoftwareRasterizer_WideBegin(int slot,int x,int y,int w,int h,int scale);
void SoftwareRasterizer_WideContext(int slot,int centred,int field,int shift,float qx,float qy);
void SoftwareRasterizer_WideReset(void);
void SoftwareRasterizer_WideReject(void);
unsigned char *SoftwareRasterizer_WideOutput(int x,int y,int w,int h,int scale,int *ow,int *oh);
void SoftwareRasterizer_SetExtensionSampler(int (*sample)(unsigned,unsigned,unsigned,unsigned,unsigned,uint16_t*));
