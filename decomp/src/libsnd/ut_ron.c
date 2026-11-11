#include <common.h>
#include <libsnd.h>
#include <libspu.h>

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtReverbOn SsUtReverbOn_Unlocked
#endif

void SsUtReverbOn(void) { SpuSetReverb(1); }

#ifdef __psyz
#undef SsUtReverbOn
void SsUtReverbOn(void) {
    Psyz_KernelLockInterrupts();
    SsUtReverbOn_Unlocked();
    Psyz_KernelUnlockInterrupts();
}
#endif
