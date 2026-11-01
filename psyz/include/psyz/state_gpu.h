#ifndef PSYZ_STATE_GPU_H
#define PSYZ_STATE_GPU_H
#include <stddef.h>
#include <stdint.h>
#define PSYZ_STATE_NATIVE_VRAM_BYTES (1024u*512u*4u)
/* Partial GPU component: native authoritative RGBA8 VRAM only; not scaled history. */
int Psyz_StateGpuBegin(void);
int Psyz_StateGpuCapture(unsigned char *,size_t);
int Psyz_StateGpuRestore(const unsigned char *,size_t);
void Psyz_StateGpuEnd(void);
int Psyz_StateDialogueReset(void);
void Psyz_StateInvalidatePgxp(void);
#define PSYZ_STATE_DUCK_BYTES (128u+64u*4u+1024u*512u/8u)
int Psyz_StateDuckCapture(unsigned char *,size_t,const unsigned char *,size_t);
int Psyz_StateDuckValidate(const unsigned char *,size_t);
int Psyz_StateDuckRestore(const unsigned char *,size_t,const unsigned char *,size_t);
#define PSYZ_STATE_GPU_CONTROL_BYTES 1024u
uint32_t Psyz_StateGpuControlBlockers(void); /* readonly diagnostic bitmask; no queue/owner mutation */
int Psyz_StateGpuControlCapture(unsigned char *,size_t);
int Psyz_StateGpuControlValidate(const unsigned char *,size_t);
int Psyz_StateGpuControlRestore(const unsigned char *,size_t);
#endif
