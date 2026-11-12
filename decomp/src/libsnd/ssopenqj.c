#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsSeqOpenJ SsSeqOpenJ_Unlocked
#endif

short SsSeqOpenJ(u_long* addr, short vab_id) {
    short bit;
    short flag;
    unsigned char exit_loop;
    u8* seq;

    flag = 0;
    seq = (u8*)addr;
    if (_snd_openflag == -1) {
        printf("Can't Open Sequence data any more\n\n");
        return -1;
    }
    bit = 0;
    exit_loop = 0;
    while (!exit_loop) {
        if ((_snd_openflag & (1u << bit)) == 0U) {
            flag = bit;
            exit_loop = 1;
        }
        bit++;
    }
    _snd_openflag |= 1u << flag;
    if (_SsInitSoundSeq(flag, vab_id, seq) == -1) {
        return -1;
    }
    return flag;
}

#ifdef __psyz
#undef SsSeqOpenJ
short SsSeqOpenJ(u_long* addr, short vab_id) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsSeqOpenJ_Unlocked(addr, vab_id);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
