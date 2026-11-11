#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSetMarkCallback SsSetMarkCallback_Unlocked
#endif

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/ssmark", SsSetMarkCallback);
#else
/* Called by _SsContNrpn1 for each mark event (NRPN 40) of the sequence. */
void SsSetMarkCallback(
    short access_num, short seq_num, SsMarkCallbackProc proc) {
    if (access_num < 0 || access_num >= LEN(_SsMarkCallback) || seq_num < 0 ||
        seq_num >= LEN(*_SsMarkCallback)) {
        return;
    }
    _SsMarkCallback[access_num][seq_num] = (SndSsMarkCallbackProc)proc;
}
#endif

#ifdef __psyz
#undef SsSetMarkCallback
void SsSetMarkCallback(
    short access_num, short seq_num, SsMarkCallbackProc proc) {
    Psyz_KernelLockInterrupts();
    SsSetMarkCallback_Unlocked(access_num, seq_num, proc);
    Psyz_KernelUnlockInterrupts();
}
#endif
