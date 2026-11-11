#include <common.h>
#include <libsnd.h>

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtReverbOff SsUtReverbOff_Unlocked
#endif

s32 SpuSetReverb(s32);

void SsUtReverbOff(void) { SpuSetReverb(0); }

#ifdef __psyz
#undef SsUtReverbOff
void SsUtReverbOff(void) {
    Psyz_KernelLockInterrupts();
    SsUtReverbOff_Unlocked();
    Psyz_KernelUnlockInterrupts();
}
#endif
