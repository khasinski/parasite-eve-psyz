#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_0", _SsContBankChange);
#else
/* Bank change: the next data byte selects the VAB. */
void _SsContBankChange(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];

    score->vab_id = *score->seq_ptr++;
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
