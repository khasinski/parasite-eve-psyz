#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtGetProgAtr SsUtGetProgAtr_Unlocked
#endif

short SsUtGetProgAtr(short vabId, short prog, ProgAtr* pProgAttr) {
    if (_svm_vab_used[vabId] == 1) {
        _SsVmVSetUp(vabId, prog);
        pProgAttr->tones = _svm_pg[prog].tones;
        pProgAttr->mvol = _svm_pg[prog].mvol;
        pProgAttr->prior = _svm_pg[prog].prior;
        pProgAttr->mode = _svm_pg[prog].mode;
        pProgAttr->mpan = _svm_pg[prog].mpan;
        pProgAttr->attr = _svm_pg[prog].attr;
        return 0;
    }
    return -1;
}

#ifdef __psyz
#undef SsUtGetProgAtr
short SsUtGetProgAtr(short vabId, short prog, ProgAtr* pProgAttr) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsUtGetProgAtr_Unlocked(vabId, prog, pProgAttr);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
