#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_100", _SsContRpn1);
#else
/* RPN LSB (CC#100): 0 = pitch bend range. */
void _SsContRpn1(short sep_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];

    score->unk1B = value;
    score->unk1D++;
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
