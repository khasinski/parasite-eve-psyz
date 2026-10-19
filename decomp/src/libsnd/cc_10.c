#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_10", _SsContPanpot);
#else
void _SsContPanpot(short sep_access_num, short seq_num, unsigned char pan) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    int ch = score->channel_idx;

    _SsVmSetVol(sep_access_num | (seq_num << 8), score->vab_id,
                score->programs[ch], score->vol[ch], pan);
    score->panpot[ch] = pan;
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
