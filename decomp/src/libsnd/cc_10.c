#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_10", _SsContPanpot);
#else
/* CC#10: channel pan. */
void _SsContPanpot(short seq_access_num, short seq_num, unsigned char pan) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 channel = score->channel_idx;

    _SsVmSetVol(seq_access_num | (seq_num << 8), score->vab_id,
                score->programs[channel], score->vol[channel], pan);
    score->panpot[channel] = pan;
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
