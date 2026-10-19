#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/next", _SsSndNextSep);

#else
/* Start the SEP track queued with SsSetNext once the current one ends. */
void _SsSndNextSep(short sep_access_num, short seq_num) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (score == NULL) {
        return;
    }
    score->l_count = 1;
    score->unk21 = 0;
    score->flags &= ~(SEQ_FLAG_100 | SEQ_FLAG_8 | SEQ_FLAG_2 | SEQ_FLAG_4 |
                      SEQ_FLAG_200);
    score->play_mode = SSPLAY_PLAY;
    score->seq_ptr = score->read_pos;
    score->flags |= SEQ_FLAG_1;
}
#endif
