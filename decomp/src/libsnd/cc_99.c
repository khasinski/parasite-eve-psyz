#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_99", _SsContNrpn2);
#else
/* NRPN MSB (CC#99): 20 = loop start, 30 = loop end, 40 = mark, otherwise the
 * tone number for a VAB attribute edit. */
void _SsContNrpn2(short sep_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];

    switch (value) {
    case 20:
        score->unk19 = value;
        score->unk1E = 1;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        score->loop_pos = score->seq_ptr;
        break;
    case 30:
        score->unk19 = value;
        if (score->unk15 == 0) {
            score->unk1F = 0;
            score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        } else if (score->unk15 < 127) {
            score->unk15--;
            score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
            if (score->unk15 != 0) {
                score->seq_ptr = score->loop_pos;
            } else {
                score->unk1F = 0;
            }
        } else {
            /* 127 loops forever */
            _SsReadDeltaValue(sep_access_num, seq_num);
            score->delta_value = 0;
            score->seq_ptr = score->loop_pos;
        }
        break;
    default:
        score->unk19 = value;
        score->unk18++;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        break;
    }
}
#endif
