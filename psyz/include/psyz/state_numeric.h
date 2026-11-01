/* Selected numeric component only, not complete GTE/GPU savestate. */
#ifndef PSYZ_STATE_NUMERIC_H
#define PSYZ_STATE_NUMERIC_H
#include <stddef.h>
size_t Psyz_GteNumericStateBytes(void);
int Psyz_GteCaptureNumericState(unsigned char *,size_t);
int Psyz_GteValidateNumericState(const unsigned char *,size_t);
int Psyz_GteRestoreNumericState(const unsigned char *,size_t);
#endif
