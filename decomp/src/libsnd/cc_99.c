#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_99", _SsContNrpn2);
#else
/* CC#99: NRPN MSB. 20 marks the loop start (its count follows as data
 * entry), 30 the loop end; a count of 127 or more loops forever. */
void _SsContNrpn2(short seq_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];

    switch (value) {
    case 20:
        score->unk1B = value;
        score->unk1C = 1;
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
        score->next_sep_pos = score->seq_ptr;
        break;
    case 30:
        score->unk1B = value;
        if (score->unk1D == 0) {
            score->unk15 = 0;
            score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
        } else if (score->unk1D < 127) {
            score->unk1D--;
            score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
            if (score->unk1D) {
                score->seq_ptr = score->next_sep_pos;
            } else {
                score->unk15 = 0;
            }
        } else {
            _SsReadDeltaValue(seq_access_num, seq_num);
            score->delta_value = 0;
            score->seq_ptr = score->next_sep_pos;
        }
        break;
    default:
        score->unk1B = value;
        score->unk1F++;
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
        break;
    }
}
#endif
