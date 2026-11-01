#ifndef PSYZ_STATE_AUDIO_H
#define PSYZ_STATE_AUDIO_H
#include <stddef.h>
int Psyz_StateKernelBegin(void);
void Psyz_StateKernelEnd(void);
int Psyz_StateAudioBegin(void);
void Psyz_StateAudioEnd(void);
int Psyz_StateAudioOwned(void);
int Psyz_StateAudioDiscardQueued(void);
int Psyz_StateAudioClockCapture(unsigned char *,size_t);
int Psyz_StateAudioClockValidate(const unsigned char *,size_t);
int Psyz_StateAudioClockRestore(const unsigned char *,size_t);
size_t Psyz_StateSpuBytes(void);
int Psyz_StateSpuCapture(unsigned char *,size_t);
int Psyz_StateSpuValidate(const unsigned char *,size_t);
int Psyz_StateSpuRestore(const unsigned char *,size_t);
#endif
