#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/tempo", _SsSndTempo);

#else
/* Per-tick accelerando/ritardando step (flags 0x40/0x80).
 *   unk4E  step: > 0 ticks per BPM change, < 0 BPM change per tick
 *   unkA8  ticks left, unkAC target BPM, unk94 current BPM */
void _SsSndTempo(short sep_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    int step = score->unk4E;
    unsigned int cur;
    unsigned int target = score->unkAC;
    unsigned int ticks;

    score->unkA8--;
    cur = score->unk94;
    if (step > 0) {
        if ((unsigned int)score->unkA8 % step != 0) {
            return;
        }
        if (target < cur) {
            score->unk94 = cur - 1;
        } else if (cur < target) {
            score->unk94 = cur + 1;
        }
    } else if (target < cur) {
        score->unk94 = cur + step;
        if ((unsigned int)score->unk94 < target) {
            score->unk94 = target;
        }
    } else if (cur < target) {
        score->unk94 = cur - step;
        if (target < (unsigned int)score->unk94) {
            score->unk94 = target;
        }
    }
    ticks = (score->resolution * score->unk94 * 10) / (VBLANK_MINUS * 60);
    score->unk54 = (short)ticks;
    if ((short)ticks <= 0) {
        score->unk54 = 1;
    }
    if (score->unkA8 == 0 || (unsigned int)score->unk94 == target) {
        score->flags &= ~(0x40 | 0x80);
    }
}
#endif
