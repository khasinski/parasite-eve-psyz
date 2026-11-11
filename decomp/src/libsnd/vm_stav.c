#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_stav", _SsVmSelectToneAndVag);
#else
/* Collects the tones of the current program whose key range holds the
 * current note, and their VAG numbers. */
unsigned char _SsVmSelectToneAndVag(unsigned char* tones, unsigned char* vags) {
    unsigned char n = 0;
    s8 i;
    VagAtr* tn;

    for (i = 0; i < _svm_cur.prog_tones; i++) {
        tn = &_svm_tn[_svm_cur.fake_program * 16 + i];
        if (!(tn->min > _svm_cur.note) && !(tn->max < _svm_cur.note)) {
            vags[n] = tn->vag;
            tones[n++] = i;
        }
    }
    return n;
}
#endif
