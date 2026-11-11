#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midiprog", _SsSetProgramChange);
#else
void _SsSetProgramChange(
    short seq_access_num, short seq_num, unsigned char prog) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];

    score->programs[score->channel_idx] = prog;
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
