#include "libsnd_private.h"

void SsSetLoop(short access_num, short seq_num, short l_count) {
#ifdef __psyz
    if (_SsScore(access_num, seq_num) == NULL) {
        return;
    }
#endif
    struct SeqStruct* score = &_ss_score[access_num][seq_num];
    score->l_count = l_count;
    score->unk21 = 0;
}

short SsIsEos(short access_num, short seq_num) {
#ifdef __psyz
    if (_SsScore(access_num, seq_num) == NULL) {
        return 0;
    }
#endif
    struct SeqStruct* score = &_ss_score[access_num][seq_num];
    return score->play_mode;
}
