#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSetReservedVoice SsSetReservedVoice_Unlocked
#endif

char SsSetReservedVoice(char voices) {
#ifdef __psyz
    if ((u8)(voices - 1) >= NUM_VOICES) {
#else
    if (voices > NUM_VOICES || voices == 0) {
#endif
        return -1;
    }
    _SsVmMaxVoice = voices;
    return _SsVmMaxVoice;
}

#ifdef __psyz
#undef SsSetReservedVoice
char SsSetReservedVoice(char voices) {
    char ret;

    Psyz_KernelLockInterrupts();
    ret = SsSetReservedVoice_Unlocked(voices);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
