#ifndef PSYZ_GPUDUMP_H
#define PSYZ_GPUDUMP_H
/* Internal hooks of the GPU dump recorder (gpudump.c), called by libgpu. */
#include <libgpu.h>

int GpuDump_Active(void);
void GpuDump_TrackDrawState(u32 word);
void GpuDump_TrackDisplay(u32 word);
void GpuDump_GP0(const u_long* words, int count);
void GpuDump_Image(const RECT* rect, const void* data);
const u32* GpuDump_DrawState(void);

/* Sends the current drawing state (E1h..E6h) to the active backend again,
 * after the platform switched between its GPU and CPU rasterizers. */
void Psyz_GpuResendDrawState(void);

#endif
