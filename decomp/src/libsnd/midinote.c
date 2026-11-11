#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midinote", _SsNoteOn);
#else
/* Note on (a zero velocity is a note off) on the current channel. */
void _SsNoteOn(short seq_access_num, short seq_num, unsigned char note,
               unsigned char vel) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 channel = score->channel_idx;
    u8 pan = score->panpot[channel];

    if (vel) {
        if (!((score->channel_mute >> channel) & 1)) {
            _SsVmKeyOn(seq_access_num | (seq_num << 8), score->vab_id,
                       score->programs[channel], note, vel, pan);
        }
    } else {
        _SsVmKeyOff(seq_access_num | (seq_num << 8), score->vab_id,
                    score->programs[channel], note);
    }
}
#endif
