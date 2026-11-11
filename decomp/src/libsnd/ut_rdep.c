#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtSetReverbDepth SsUtSetReverbDepth_Unlocked
#endif

void SsUtSetReverbDepth(short ldepth, short rdepth) {
    _svm_rattr.mask = 6;
    _svm_rattr.depth.left = (ldepth * 0x7FFF) / 127;
    _svm_rattr.depth.right = (rdepth * 0x7FFF) / 127;
    SpuSetReverbModeParam(&_svm_rattr);
}

#ifdef __psyz
#undef SsUtSetReverbDepth
void SsUtSetReverbDepth(short ldepth, short rdepth) {
    Psyz_KernelLockInterrupts();
    SsUtSetReverbDepth_Unlocked(ldepth, rdepth);
    Psyz_KernelUnlockInterrupts();
}
#endif
