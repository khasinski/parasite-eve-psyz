#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midimeta", _SsGetMetaEvent);
#else
void _SsGetMetaEvent(short sep_access_num, short seq_num, unsigned char type) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    unsigned int usec;
    unsigned int ticks;
    u8 hi, mid;

    switch (type) {
    case 0x51: /* set tempo: 3 bytes of microseconds per quarter note */
        hi = *score->seq_ptr++;
        mid = *score->seq_ptr++;
        usec = (hi << 16) | (mid << 8) | *score->seq_ptr++;
        if (usec == 0) {
            usec = 1;
        }
        score->unk94 = 60000000 / usec;
        if (score->unk94 == 0) {
            score->unk94 = 1;
        }
        ticks = score->resolution * score->unk94;
        if (ticks == 0) {
            ticks = 1;
        }
        if (ticks * 10 < VBLANK_MINUS * 60) {
            score->unk54 = score->unk52 = (VBLANK_MINUS * 600) / ticks;
        } else {
            score->unk52 = -1;
            score->unk54 = (ticks * 10) / (VBLANK_MINUS * 60);
            if ((VBLANK_MINUS * 30) < (ticks * 10) % (VBLANK_MINUS * 60)) {
                score->unk54++;
            }
        }
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        break;
    case 0x2F: /* end of track */
        _SsSeqGetEof(sep_access_num, seq_num);
        break;
    }
}
#endif
