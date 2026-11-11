#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midibend", _SsSetPitchBend);
#else
/* _SsGetSeqData already skipped the LSB; only the MSB is used. */
void _SsSetPitchBend(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 value = *score->seq_ptr++;

    _SsVmPitchBend(seq_access_num | (seq_num << 8), score->vab_id,
                   score->programs[score->channel_idx], value);
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
