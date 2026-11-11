#include "libsnd_private.h"

void _SsSndSetPauseMode(short sep_access_num, short seq_num) {
    _ss_score[sep_access_num][seq_num].flags |= SEQ_FLAG_2;
}

void SsSeqPause(short seq_access_num) {
    _SsSndSetPauseMode(seq_access_num, 0);
}

void SsSepPause(short sep_access_num, short seq_num) {
    _SsSndSetPauseMode(sep_access_num, seq_num);
}
