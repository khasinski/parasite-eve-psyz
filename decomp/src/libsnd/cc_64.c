#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_64", _SsContDamper);
#else
void _SsContDamper(short sep_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];

    if (value < 64) {
        _SsVmDamperOff();
    } else {
        _SsVmDamperOn();
    }
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
