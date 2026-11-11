#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtAutoPan SsUtAutoPan_Unlocked
#endif

short SsUtAutoPan(
    short voice, short start_pan, short end_pan, short delta_time) {
    _autopan = SetAutoPan;
    if (voice >= 0 && voice < NUM_VOICES) {
        SeAutoPan(voice, start_pan, end_pan, delta_time);
        return 0;
    }
    return -1;
}

#ifdef __psyz
#undef SsUtAutoPan
short SsUtAutoPan(
    short voice, short start_pan, short end_pan, short delta_time) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsUtAutoPan_Unlocked(voice, start_pan, end_pan, delta_time);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
