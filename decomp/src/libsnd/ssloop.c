#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSetLoop SsSetLoop_Unlocked
#endif

void SsSetLoop(short access_num, short seq_num, short l_count) {
    struct SeqStruct* score = &_ss_score[access_num][seq_num];
    score->l_count = l_count;
    score->unk21 = 0;
}

short SsIsEos(short access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[access_num][seq_num];
    return score->play_mode;
}

#ifdef __psyz
#undef SsSetLoop
void SsSetLoop(short access_num, short seq_num, short l_count) {
    Psyz_KernelLockInterrupts();
    SsSetLoop_Unlocked(access_num, seq_num, l_count);
    Psyz_KernelUnlockInterrupts();
}
#endif
