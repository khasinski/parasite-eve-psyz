#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_64", _SsContDamper);
#else
/* CC#64: damper pedal, slowing the release of new voices. */
void _SsContDamper(short seq_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];

    if (value < 64) {
        _SsVmDamperOff();
    } else {
        _SsVmDamperOn();
    }
    score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
}
#endif
