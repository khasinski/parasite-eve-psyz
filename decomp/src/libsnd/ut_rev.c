#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtSetReverbType SsUtSetReverbType_Unlocked
#endif

short SsUtSetReverbType(short type) {
    short _type;
    int is_neg;

    is_neg = 0;
    _type = type;
    if (type < 0) {
        is_neg = 1;
        _type = -type;
    }

    if (_type >= 0 && _type < 10) {
        _svm_rattr.mask = 1;
        if (is_neg) {
            _svm_rattr.mode = _type | 0x100;
        } else {
            _svm_rattr.mode = _type;
        }
        if (_type == 0) {
            SpuSetReverb(0);
        }
        SpuSetReverbModeParam(&_svm_rattr);
        return _type;
    }
    return -1;
}

short SsUtGetReverbType(void) { return _svm_rattr.mode; }

#ifdef __psyz
#undef SsUtSetReverbType
short SsUtSetReverbType(short type) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsUtSetReverbType_Unlocked(type);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
