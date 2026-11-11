#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/next", _SsSndNextSep);
#else
/* Starts the SEP chained with SsSetNext once the current one ends. */
void _SsSndNextSep(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];

    score->l_count = 1;
    score->unk21 = 0;
    score->flags &= ~SEQ_FLAG_100;
    score->flags &= ~SEQ_FLAG_8;
    score->flags &= ~SEQ_FLAG_2;
    score->flags &= ~SEQ_FLAG_4;
    score->flags &= ~SEQ_FLAG_200;
    score->play_mode = SSPLAY_PLAY;
    score->seq_ptr = score->read_pos;
    score->flags |= SEQ_FLAG_1;
}
#endif
