#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_no1", vmNoiseOn);

#else
/* Key on the current tone (_svm_cur) on voice as SPU noise; the note selects
 * the noise clock. */
void vmNoiseOn(char voice) {
    _svm_cur.voice = voice;
    SpuSetNoiseClock(_svm_cur.note & 0x3F);
    SpuSetNoiseVoice(SPU_ON, 1 << voice);
    _SsVmKeyOnNow(1, 0x1000);
    _svm_voice[(int)voice].unk1b = 2;
}
#endif
