#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSeqPause SsSeqPause_Unlocked
#define SsSepPause SsSepPause_Unlocked
#endif

#ifndef __psyz
void _SsSndSetPauseMode(short sep_access_num, short seq_num) {
    _ss_score[sep_access_num][seq_num].flags |= SEQ_FLAG_2;
}

void SsSeqPause(short seq_access_num) { _SsSndSetPauseMode(seq_access_num, 0); }

void SsSepPause(short sep_access_num, short seq_num) {
    _SsSndSetPauseMode(sep_access_num, seq_num);
}
#else
/* PSY-Q 4.x stops the tick at once (flag 1) and saves the volume; the next
 * tick keys the voices off (_SsSndPause). */
void _SsSndSetPauseMode(short sep_access_num, short seq_num) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (!score) {
        return;
    }
    _SsVmGetSeqVol(
        sep_access_num | (seq_num << 8), &score->unk5C, &score->unk5E);
    score->flags &= ~SEQ_FLAG_1;
    score->flags &= ~SEQ_FLAG_8;
    score->flags |= SEQ_FLAG_2;
}

void SsSeqPause(short seq_access_num) { _SsSndSetPauseMode(seq_access_num, 0); }

void SsSepPause(short sep_access_num, short seq_num) {
    _SsSndSetPauseMode(sep_access_num, seq_num);
}
#endif

#ifdef __psyz
#undef SsSeqPause
void SsSeqPause(short seq_access_num) {
    Psyz_KernelLockInterrupts();
    SsSeqPause_Unlocked(seq_access_num);
    Psyz_KernelUnlockInterrupts();
}

#undef SsSepPause
void SsSepPause(short sep_access_num, short seq_num) {
    Psyz_KernelLockInterrupts();
    SsSepPause_Unlocked(sep_access_num, seq_num);
    Psyz_KernelUnlockInterrupts();
}
#endif
