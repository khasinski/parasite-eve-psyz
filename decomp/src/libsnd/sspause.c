#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/sspause", _SsSndSetPauseMode);

INCLUDE_ASM("asm/nonmatchings/libsnd/sspause", SsSeqPause);

INCLUDE_ASM("asm/nonmatchings/libsnd/sspause", SsSepPause);

#else
static void _SsSndSetPauseMode(short sep_access_num, short seq_num) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (score == NULL) {
        return;
    }
    _SsVmGetSeqVol(sep_access_num | (seq_num << 8), (short*)&score->voll,
                   (short*)&score->volr);
    score->flags &= ~(SEQ_FLAG_1 | SEQ_FLAG_8);
    score->flags |= SEQ_FLAG_2;
}

void SsSeqPause(short seq_access_num) {
    _SsSndSetPauseMode(seq_access_num, 0);
}

void SsSepPause(short sep_access_num, short seq_num) {
    _SsSndSetPauseMode(sep_access_num, seq_num);
}
#endif
