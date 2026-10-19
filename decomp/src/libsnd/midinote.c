#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midinote", _SsNoteOn);
#else
void _SsNoteOn(short sep_access_num, short seq_num, unsigned char note,
               unsigned char vel) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    int ch = score->channel_idx;
    int seq_sep_no = sep_access_num | (seq_num << 8);

    if ((score->channel_mute >> ch) & 1) {
        return;
    }
    if (vel != 0) {
        _SsVmKeyOn(seq_sep_no, score->vab_id, score->programs[ch], note, vel,
                   score->panpot[ch]);
    } else {
        _SsVmKeyOff(seq_sep_no, score->vab_id, score->programs[ch], note);
    }
}
#endif
