#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midibend", _SsSetPitchBend);
#else
/* The status reader already skipped the LSB; only the MSB is used. */
void _SsSetPitchBend(short sep_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    unsigned char msb = *score->seq_ptr++;

    _SsVmPitchBend(sep_access_num | (seq_num << 8), score->vab_id,
                   score->programs[score->channel_idx], msb);
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
