#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/ssreplay", _SsSndSetReplayMode);

INCLUDE_ASM("asm/nonmatchings/libsnd/ssreplay", SsSeqReplay);

INCLUDE_ASM("asm/nonmatchings/libsnd/ssreplay", SsSepReplay);

#else
static void _SsSndSetReplayMode(short sep_access_num, short seq_num) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (score == NULL) {
        return;
    }
    if (!(score->flags & (SEQ_FLAG_200 | SEQ_FLAG_4)) &&
        !(score->flags & SEQ_FLAG_100)) {
        score->flags &= ~SEQ_FLAG_2;
        score->flags |= SEQ_FLAG_8 | SEQ_FLAG_1;
    }
}

void SsSeqReplay(short seq_access_num) {
    _SsSndSetReplayMode(seq_access_num, 0);
}

void SsSepReplay(short sep_access_num, short seq_num) {
    _SsSndSetReplayMode(sep_access_num, seq_num);
}
#endif
