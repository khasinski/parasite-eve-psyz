#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSeqCalledTbyT SsSeqCalledTbyT_Unlocked
#endif

void SsSeqCalledTbyT(void) {
    int seq_index;
    int tIndex;
    int bit;

    if (_snd_ev_flag == 1) {
        return;
    }
    _snd_ev_flag = 1;
    _SsVmFlush();

    for (seq_index = 0; seq_index < _snd_seq_s_max; seq_index++) {
        bit = 1u << seq_index;
        if (!(_snd_openflag & bit)) {
            continue;
        }
        for (tIndex = 0; tIndex < _snd_seq_t_max; tIndex++) {
            if (_ss_score[seq_index][tIndex].flags & 1) {
                _SsSndPlay(seq_index, tIndex);
                if (_ss_score[seq_index][tIndex].flags & 0x10) {
                    _SsSndCrescendo(seq_index, tIndex);
                }
                if (_ss_score[seq_index][tIndex].flags & 0x20) {
                    _SsSndDecrescendo(seq_index, tIndex);
                }
                if (_ss_score[seq_index][tIndex].flags & 0x40) {
                    _SsSndTempo(seq_index, tIndex);
                }
                if (_ss_score[seq_index][tIndex].flags & 0x80) {
                    _SsSndTempo(seq_index, tIndex);
                }
            }
            if (_ss_score[seq_index][tIndex].flags & 2) {
                _SsSndPause(seq_index, tIndex);
            }
            if (_ss_score[seq_index][tIndex].flags & 8) {
                _SsSndReplay(seq_index, tIndex);
            }
            if (_ss_score[seq_index][tIndex].flags & 4) {
                _SsSndStop(seq_index, tIndex);
                _ss_score[seq_index][tIndex].flags = 0;
            }
        }
    }
    _snd_ev_flag = 0;
}

#ifdef __psyz
#undef SsSeqCalledTbyT
/* A tick's PS1 time, mostly _SsVmFlush's pass over the 24 voices
 * (DuckStation: 6.3k system clocks with no sequence playing). */
enum { SS_TICK_CYCLES = 6335 };
void Psyz_SpendCycles(unsigned cycles);
void SsSeqCalledTbyT(void) {
    Psyz_KernelLockInterrupts();
    SsSeqCalledTbyT_Unlocked();
    Psyz_SpendCycles(SS_TICK_CYCLES);
    Psyz_KernelUnlockInterrupts();
}
#endif
