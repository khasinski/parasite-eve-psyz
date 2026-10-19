#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/ssdecres", _SsSndSetDecres);

INCLUDE_ASM("asm/nonmatchings/libsnd/ssdecres", SsSeqSetDecrescendo);

INCLUDE_ASM("asm/nonmatchings/libsnd/ssdecres", SsSepSetDecrescendo);

#else
static void _SsSndSetDecres(
    short sep_access_num, short seq_num, short vol, long v_time) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (score == NULL) {
        return;
    }
    _SsSndSetVolData(sep_access_num, seq_num, vol, v_time);
    score->flags |= SEQ_FLAG_20;
    score->flags &= ~SEQ_FLAG_10;
}

void SsSeqSetDecrescendo(short seq_access_num, short vol, long v_time) {
    _SsSndSetDecres(seq_access_num, 0, vol, v_time);
}

void SsSepSetDecrescendo(
    short sep_access_num, short seq_num, short vol, long v_time) {
    _SsSndSetDecres(sep_access_num, seq_num, vol, v_time);
}
#endif
