#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSeqReplay SsSeqReplay_Unlocked
#define SsSepReplay SsSepReplay_Unlocked
#endif

#ifndef __psyz
void _SsSndSetReplayMode(short sep_access_num, short seq_num) {
    _ss_score[sep_access_num][seq_num].flags |= SEQ_FLAG_8;
}

void SsSeqReplay(short seq_access_num) {
    _SsSndSetReplayMode(seq_access_num, 0);
}

void SsSepReplay(short sep_access_num, short seq_num) {
    _SsSndSetReplayMode(sep_access_num, seq_num);
}
#else
/* Resumes a paused sequence, unless it ended (flags 4/0x200) or waits as the
 * next SEP of another (0x100). */
void _SsSndSetReplayMode(short sep_access_num, short seq_num) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (!score) {
        return;
    }
    if (!(score->flags & (SEQ_FLAG_200 | SEQ_FLAG_4)) &&
        !(score->flags & SEQ_FLAG_100)) {
        score->flags &= ~SEQ_FLAG_2;
        score->flags |= SEQ_FLAG_8;
        score->flags |= SEQ_FLAG_1;
    }
}

void SsSeqReplay(short seq_access_num) {
    _SsSndSetReplayMode(seq_access_num, 0);
}

void SsSepReplay(short sep_access_num, short seq_num) {
    _SsSndSetReplayMode(sep_access_num, seq_num);
}
#endif

#ifdef __psyz
#undef SsSeqReplay
void SsSeqReplay(short seq_access_num) {
    Psyz_KernelLockInterrupts();
    SsSeqReplay_Unlocked(seq_access_num);
    Psyz_KernelUnlockInterrupts();
}

#undef SsSepReplay
void SsSepReplay(short sep_access_num, short seq_num) {
    Psyz_KernelLockInterrupts();
    SsSepReplay_Unlocked(sep_access_num, seq_num);
    Psyz_KernelUnlockInterrupts();
}
#endif
