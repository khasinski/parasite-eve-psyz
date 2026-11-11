#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midimeta", _SsGetMetaEvent);
#else
/* The only meta event of SEQ data besides the end of track (handled by
 * _SsGetSeqData) is the tempo, three bytes of microseconds per beat with no
 * length byte. */
void _SsGetMetaEvent(short seq_access_num, short seq_num, unsigned char type) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u32 b0, b1, b2, ticks;

    (void)type;
    b0 = *score->seq_ptr++;
    b1 = *score->seq_ptr++;
    b2 = *score->seq_ptr++;
    score->unk94 = 60000000 / ((b0 << 16) | (b1 << 8) | b2);
    ticks = score->resolution * score->unk94;
    if (ticks * 10 < VBLANK_MINUS * 60) {
        score->unk52 = score->unk54 = VBLANK_MINUS * 600 / ticks;
    } else {
        score->unk52 = -1;
        score->unk54 =
            score->resolution * score->unk94 * 10 / (VBLANK_MINUS * 60);
        if (VBLANK_MINUS * 30 <
            score->resolution * score->unk94 * 10 % (VBLANK_MINUS * 60)) {
            score->unk54++;
        }
    }
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
