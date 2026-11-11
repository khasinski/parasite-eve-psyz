#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSeqSetVol SsSeqSetVol_Unlocked
#define SsSepSetVol SsSepSetVol_Unlocked
#define SsSeqGetVol SsSeqGetVol_Unlocked
#endif

#ifndef __psyz
void _SsSndSetVol(short sep_access_num, short seq_num, short voll, short volr) {
    _SsVmSetSeqVol(sep_access_num | (seq_num << 8), voll, volr, 0);
}

void SsSeqSetVol(short seq_access_num, short voll, short volr) {
    _SsVmSetSeqVol(seq_access_num, voll, volr, 0);
}

void SsSepSetVol(short sep_access_num, short seq_num, short voll, short volr) {
    _SsVmSetSeqVol(sep_access_num | (seq_num << 8), voll, volr, 0);
}

void SsSeqGetVol(short access_num, short seq_num, short* voll, short* volr) {
    _SsVmGetSeqVol(access_num | (seq_num << 8), voll, volr);
}
#else
/* While a sequence plays (flags exactly 1) the volume applies to its voices
 * at once; otherwise it is stored for the next note or SsSeqPlay. */
void _SsSndSetVol(short sep_access_num, short seq_num, short voll, short volr) {
    struct SeqStruct* score = _SsScore(sep_access_num, seq_num);

    if (!score) {
        return;
    }
    if (score->flags != SEQ_FLAG_1) {
        score->voll = voll;
        score->volr = volr;
    } else {
        _SsVmSetSeqVol(sep_access_num | (seq_num << 8), voll, volr, 1);
    }
}

void SsSeqSetVol(short seq_access_num, short voll, short volr) {
    _SsSndSetVol(seq_access_num, 0, voll, volr);
}

void SsSepSetVol(short sep_access_num, short seq_num, short voll, short volr) {
    _SsSndSetVol(sep_access_num, seq_num, voll, volr);
}

void SsSeqGetVol(short access_num, short seq_num, short* voll, short* volr) {
    _SsVmGetSeqVol(access_num | (seq_num << 8), voll, volr);
}
#endif

#ifdef __psyz
#undef SsSeqSetVol
void SsSeqSetVol(short seq_access_num, short voll, short volr) {
    Psyz_KernelLockInterrupts();
    SsSeqSetVol_Unlocked(seq_access_num, voll, volr);
    Psyz_KernelUnlockInterrupts();
}

#undef SsSepSetVol
void SsSepSetVol(short sep_access_num, short seq_num, short voll, short volr) {
    Psyz_KernelLockInterrupts();
    SsSepSetVol_Unlocked(sep_access_num, seq_num, voll, volr);
    Psyz_KernelUnlockInterrupts();
}

#undef SsSeqGetVol
void SsSeqGetVol(short access_num, short seq_num, short* voll, short* volr) {
    Psyz_KernelLockInterrupts();
    SsSeqGetVol_Unlocked(access_num, seq_num, voll, volr);
    Psyz_KernelUnlockInterrupts();
}
#endif
