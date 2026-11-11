#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/tempo", _SsSndTempo);
#else
/* One tick of accelerando/ritardando: unkA8 ticks remain, unk4E is the tick
 * interval per tempo step (or minus the step per tick), unkAC the target. */
void _SsSndTempo(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    int interval;
    u32 tempo, target;
    s16 rate;

    score->unkA8--;
    if (score->unkA8 < 0) {
        score->flags &= ~0x40;
        score->flags &= ~0x80;
        return;
    }
    interval = score->unk4E;
    tempo = score->unk94;
    target = score->unkAC;
    if (interval > 0) {
        if (score->unkA8 % interval != 0) {
            return;
        }
        if (target < tempo) {
            score->unk94 = tempo - 1;
        } else if (tempo < target) {
            score->unk94 = tempo + 1;
        }
    } else if (target < tempo) {
        score->unk94 = tempo + interval;
        if (score->unk94 < target) {
            score->unk94 = target;
        }
    } else if (tempo < target) {
        score->unk94 = tempo - interval;
        if (target < score->unk94) {
            score->unk94 = target;
        }
    }
    rate = score->resolution * score->unk94 * 10 / (VBLANK_MINUS * 60);
    score->unk54 = rate > 0 ? rate : 1;
    if (score->unkA8 == 0 || score->unk94 == score->unkAC) {
        score->flags &= ~0x40;
        score->flags &= ~0x80;
    }
}
#endif
