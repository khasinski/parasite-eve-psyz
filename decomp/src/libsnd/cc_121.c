#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_121", _SsContResetAll);
#else
/* CC#121: reset the controllers of the current channel. */
void _SsContResetAll(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 channel = score->channel_idx;

    SsUtReverbOff();
    _SsVmDamperOff();
    score->programs[channel] = channel;
    score->unk18 = 0;
    score->unk19 = 0;
    score->vol[channel] = 127;
    score->panpot[channel] = 64;
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
