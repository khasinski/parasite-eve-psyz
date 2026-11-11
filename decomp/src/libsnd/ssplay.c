#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSeqPlay SsSeqPlay_Unlocked
#define SsSepPlay SsSepPlay_Unlocked
#endif

void SsSeqPlay(short seq_access_num, char play_mode, short l_count) {
    Snd_SetPlayMode(seq_access_num, 0, play_mode, l_count);
}

void SsSepPlay(
    short sep_access_num, short seq_num, char play_mode, short l_count) {
    Snd_SetPlayMode(sep_access_num, seq_num, play_mode, l_count);
}

#ifdef __psyz
#undef SsSeqPlay
void SsSeqPlay(short seq_access_num, char play_mode, short l_count) {
    Psyz_KernelLockInterrupts();
    SsSeqPlay_Unlocked(seq_access_num, play_mode, l_count);
    Psyz_KernelUnlockInterrupts();
}

#undef SsSepPlay
void SsSepPlay(
    short sep_access_num, short seq_num, char play_mode, short l_count) {
    Psyz_KernelLockInterrupts();
    SsSepPlay_Unlocked(sep_access_num, seq_num, play_mode, l_count);
    Psyz_KernelUnlockInterrupts();
}
#endif
