#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_stav", _SsVmSelectToneAndVag);

#else
/* Collect the tones of the current program whose key range covers the note. */
int _SsVmSelectToneAndVag(u8* vag_list, u8* tone_list) {
    int count = 0;
    int tone;
    VagAtr* tn;
    u8 note = _svm_cur.note;

    for (tone = 0; tone < (u8)_svm_cur.prog_tones && tone < 16; tone++) {
        tn = &_svm_tn[_svm_cur.fake_program * 16 + tone];
        if (note >= tn->min && note <= tn->max) {
            vag_list[count] = tn->vag;
            tone_list[count] = tone;
            count++;
        }
    }
    return count;
}
#endif
