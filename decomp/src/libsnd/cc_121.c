#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_121", _SsContResetAll);
#else
void _SsContResetAll(short sep_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    int ch = score->channel_idx;

    SsUtReverbOff();
    _SsVmDamperOff();
    score->programs[ch] = ch;
    score->unk1B = 0;
    score->unk1C = 0;
    score->vol[ch] = 127;
    score->panpot[ch] = 64;
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
