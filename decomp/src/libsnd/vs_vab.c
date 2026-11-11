#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsVabClose SsVabClose_Unlocked
#endif

void SsVabClose(short vab_id) {
    if (vab_id >= 0 && vab_id < NUM_VAB) {
        if (_svm_vab_used[vab_id] == 1) {
            SpuFree(_svm_vab_start[vab_id]);
            _svm_vab_used[vab_id] = 0;
            _svm_vab_count--;
        }
    }
}

#ifdef __psyz
#undef SsVabClose
void SsVabClose(short vab_id) {
    Psyz_KernelLockInterrupts();
    SsVabClose_Unlocked(vab_id);
    Psyz_KernelUnlockInterrupts();
}
#endif
