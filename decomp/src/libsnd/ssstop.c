#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSeqStop SsSeqStop_Unlocked
#define SsSepStop SsSepStop_Unlocked
#endif

void _SsSndStop(short sep_access_num, short seq_num) {
    struct SeqStruct* score;
    int i;

#ifdef __psyz
    /* PSY-Q 4.x leaves the reverb on when a sequence stops. */
    score = _SsScore(sep_access_num, seq_num);
    if (!score) {
        return;
    }
#else
    score = &_ss_score[sep_access_num][seq_num];
#endif
    _ss_score[sep_access_num][seq_num].flags &= ~SEQ_FLAG_1;
    _ss_score[sep_access_num][seq_num].flags &= ~SEQ_FLAG_2;
    _ss_score[sep_access_num][seq_num].flags &= ~SEQ_FLAG_8;
    _ss_score[sep_access_num][seq_num].flags &= ~SEQ_FLAG_400;
    _ss_score[sep_access_num][seq_num].flags |= SEQ_FLAG_4;
    _SsVmSeqKeyOff(sep_access_num | (seq_num << 8));
    _SsVmDamperOff();
#ifndef __psyz
    SsUtReverbOff();
#endif
    score->play_mode = SSPLAY_PAUSE;
    score->unk88 = 0;
    score->unk1C = 0;
    score->unk18 = 0;
    score->unk19 = 0;
    score->unk1E = 0;
    score->fn_idx = 0;
    score->unk1B = 0;
    score->unk1F = 0;
    score->channel_idx = 0;
    score->unk21 = 0;
    score->unk1C = 0;
    score->unk1D = 0;
    score->unk15 = 0;
    score->running_status = 0;
    score->delta_value = score->unk84;
    score->unk94 = score->tempo;
    score->unk54 = score->unk56;
    score->seq_ptr = score->read_pos;
    score->next_sep_pos = score->read_pos;
    for (i = 0; i < 16; i++) {
        score->programs[i] = i;
        score->panpot[i] = 64;
        score->vol[i] = 127;
    }
    score->unk5C = 127;
    score->unk5E = 127;
}

void SsSeqStop(short seq_access_num) { _SsSndStop(seq_access_num, 0); }

void SsSepStop(short sep_access_num, short seq_num) {
    _SsSndStop(sep_access_num, seq_num);
}

#ifdef __psyz
#undef SsSeqStop
void SsSeqStop(short seq_access_num) {
    Psyz_KernelLockInterrupts();
    SsSeqStop_Unlocked(seq_access_num);
    Psyz_KernelUnlockInterrupts();
}

#undef SsSepStop
void SsSepStop(short sep_access_num, short seq_num) {
    Psyz_KernelLockInterrupts();
    SsSepStop_Unlocked(sep_access_num, seq_num);
    Psyz_KernelUnlockInterrupts();
}
#endif
