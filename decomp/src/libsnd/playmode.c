#include "libsnd_private.h"

#ifndef __psyz
void Snd_SetPlayMode(
    short sep_access_num, short seq_num, u8 play_mode, short l_count) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];

    score->seq_ptr = score->read_pos;
    score->next_sep_pos = score->read_pos;
    score->loop_pos = score->read_pos;
    _ss_score[sep_access_num][seq_num].flags &= ~SEQ_FLAG_200;
    _ss_score[sep_access_num][seq_num].flags &= ~SEQ_FLAG_4;
    score->l_count = l_count;

    if (play_mode == 1) {
        _ss_score[sep_access_num][seq_num].flags |= SEQ_FLAG_1;
        score->unk21 = 0;
        score->play_mode = SSPLAY_PLAY;
        _SsVmSetSeqVol(
            sep_access_num | (seq_num << 8), score->voll, score->volr, 0);
    } else if (play_mode == 0) {
        _ss_score[sep_access_num][seq_num].flags |= SEQ_FLAG_2;
    }
}
#else
/* PSY-Q 4.x applies the stored sequence volume to the voices when playback
 * starts. */
void Snd_SetPlayMode(
    short sep_access_num, short seq_num, u8 play_mode, short l_count) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (!score) {
        return;
    }
    score->seq_ptr = score->read_pos;
    score->next_sep_pos = score->read_pos;
    score->loop_pos = score->read_pos;
    score->flags &= ~SEQ_FLAG_200;
    score->flags &= ~SEQ_FLAG_4;
    score->l_count = l_count;
    if ((s8)play_mode == SSPLAY_PLAY) {
        score->flags |= SEQ_FLAG_1;
        score->play_mode = SSPLAY_PLAY;
        score->unk21 = 0;
        _SsVmSetSeqVol(
            sep_access_num | (seq_num << 8), score->voll, score->volr, 1);
    } else if ((s8)play_mode == SSPLAY_PAUSE) {
        score->flags |= SEQ_FLAG_2;
    }
}
#endif
