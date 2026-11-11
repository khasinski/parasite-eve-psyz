#include "libsnd_private.h"

void _SsSndSetReplayMode(short sep_access_num, short seq_num) {
    _ss_score[sep_access_num][seq_num].flags |= SEQ_FLAG_8;
}

void SsSeqReplay(short seq_access_num) {
    _SsSndSetReplayMode(seq_access_num, 0);
}

void SsSepReplay(short sep_access_num, short seq_num) {
    _SsSndSetReplayMode(sep_access_num, seq_num);
}
