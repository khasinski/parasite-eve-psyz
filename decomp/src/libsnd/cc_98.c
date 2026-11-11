#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_98", _SsContNrpn1);
#else
/* CC#98: NRPN LSB. After NRPN MSB 40 it is the data of a mark event,
 * passed to the SsSetMarkCallback handler; MSB 20 and 30 are loop marks.
 * Otherwise it selects the VAB attribute set by the next data entry. */
void _SsContNrpn1(short seq_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    SndSsMarkCallbackProc mark;

    if (score->unk1B == 40) {
        mark = _SsMarkCallback[seq_access_num][seq_num];
        if (mark) {
            mark(seq_access_num, seq_num, value);
        }
    }
    if (score->unk1B != 30 && score->unk1B != 20 && score->unk1B != 40) {
        score->fn_idx = value;
        score->unk1C = 0;
        score->unk1F++;
    }
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
