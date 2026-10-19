#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_101", _SsContRpn2);
#else
/* RPN MSB (CC#101). */
void _SsContRpn2(short sep_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];

    score->unk1C = value;
    score->unk1D++;
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
