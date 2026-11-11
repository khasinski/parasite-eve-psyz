#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtChangeADSR SsUtChangeADSR_Unlocked
#endif

short SsUtChangeADSR(short voice, short vabId, short prog, short old_note,
                     unsigned short adsr1, unsigned short adsr2) {
    volatile int dummy;
    if (voice >= 0 && voice < NUM_VOICES) {
        if (_svm_voice[voice].vabId == vabId &&
            _svm_voice[voice].prog == prog &&
            _svm_voice[voice].note == old_note) {
            _svm_sreg_buf[voice].adsr[0] = adsr1;
            _svm_sreg_buf[voice].adsr[1] = adsr2;
            _svm_sreg_dirty[voice] |= 0x30;
            return 0;
        }
    }
    return -1;
}

#ifdef __psyz
#undef SsUtChangeADSR
short SsUtChangeADSR(short voice, short vabId, short prog, short old_note,
                     unsigned short adsr1, unsigned short adsr2) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsUtChangeADSR_Unlocked(voice, vabId, prog, old_note, adsr1, adsr2);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
