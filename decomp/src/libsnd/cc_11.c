#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_11", _SsContExpression);
#else
/* Expression scales the channel's main volume without replacing it. */
void _SsContExpression(
    short sep_access_num, short seq_num, unsigned char expr) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    int ch = score->channel_idx;

    _SsVmSetVol(sep_access_num | (seq_num << 8), score->vab_id,
                score->programs[ch], score->vol[ch] * expr / 127,
                score->panpot[ch]);
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
