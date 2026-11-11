#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtSetDetVVol SsUtSetDetVVol_Unlocked
#define SsUtSetVVol SsUtSetVVol_Unlocked
#endif

INCLUDE_ASM("asm/nonmatchings/libsnd/ut_vvol", SsUtGetDetVVol);

short SsUtSetDetVVol(short voice, short voll, short volr) {
    volatile int dummy;
    if (voice >= 0 && voice < NUM_VOICES) {
        _svm_sreg_buf[voice].volume.right = volr;
        _svm_sreg_buf[voice].volume.left = voll;
        _svm_sreg_dirty[voice] |= 3;
        return 0;
    }
    return -1;
}

short SsUtGetVVol(short voice, short* voll, short* volr) {
    if (voice >= 0 && voice < NUM_VOICES) {
        int r = (short)SPURV(voice, volume.right);
        *voll = (short)SPURV(voice, volume.left) / 129;
        *volr = r / 129;
        return 0;
    }
    return -1;
}

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/ut_vvol", SsUtSetVVol);
#else
short SsUtSetVVol(short voice, short voll, short volr) {
    volatile int dummy;
    unsigned short l;
    unsigned short r;
    if (voice >= 0 && voice < NUM_VOICES) {
        /* PSY-Q 4.x sets the linear volume, without the squared curve of
         * the sequence voices. */
        l = voll * 0x81;
        r = volr * 0x81;
        _svm_sreg_buf[voice].volume.right = r;
        _svm_sreg_buf[voice].volume.left = l;
        _svm_sreg_dirty[voice] |= 3;
        return 0;
    }
    return -1;
}
#endif

#ifdef __psyz
#undef SsUtSetDetVVol
short SsUtSetDetVVol(short voice, short voll, short volr) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsUtSetDetVVol_Unlocked(voice, voll, volr);
    Psyz_KernelUnlockInterrupts();
    return ret;
}

#undef SsUtSetVVol
short SsUtSetVVol(short voice, short voll, short volr) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsUtSetVVol_Unlocked(voice, voll, volr);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
