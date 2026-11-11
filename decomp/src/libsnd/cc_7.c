#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_7", _SsContMainVol);
#else
/* CC#7: channel volume, applied to the sounding voices at once. */
void _SsContMainVol(short seq_access_num, short seq_num, unsigned char vol) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 channel = score->channel_idx;

    _SsVmSetVol(seq_access_num | (seq_num << 8), score->vab_id,
                score->programs[channel], vol, score->panpot[channel]);
    score->vol[channel] = vol;
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
