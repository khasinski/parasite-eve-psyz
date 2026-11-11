#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtSetReverbDelay SsUtSetReverbDelay_Unlocked
#endif

void SsUtSetReverbDelay(short delay) {
    _svm_rattr.mask = SPU_REV_DELAYTIME;
    _svm_rattr.delay = delay;
    SpuSetReverbModeParam(&_svm_rattr);
}

#ifdef __psyz
#undef SsUtSetReverbDelay
void SsUtSetReverbDelay(short delay) {
    Psyz_KernelLockInterrupts();
    SsUtSetReverbDelay_Unlocked(delay);
    Psyz_KernelUnlockInterrupts();
}
#endif
