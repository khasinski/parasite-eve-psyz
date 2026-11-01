#ifndef PSYZ_STATE_PRODUCERS_H
#define PSYZ_STATE_PRODUCERS_H
#include <stddef.h>
size_t Psyz_StateCdBytes(void);
int Psyz_StateCdCapture(unsigned char *,size_t);
int Psyz_StateCdValidate(const unsigned char *,size_t);
int Psyz_StateCdRestore(const unsigned char *,size_t);
int Psyz_StateKernelOwned(void);
size_t Psyz_StateKernelBytes(void);
int Psyz_StateKernelCapture(unsigned char *,size_t);
int Psyz_StateKernelValidate(const unsigned char *,size_t);
int Psyz_StateKernelRestore(const unsigned char *,size_t);
#endif
