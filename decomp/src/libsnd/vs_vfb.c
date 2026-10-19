#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vs_vfb", SsVabFakeBody);

#else
/* The VAB body was transferred by the caller: mark the bank ready. */
short SsVabFakeBody(short vabid) {
    if (vabid >= 0 && vabid < NUM_VAB && _svm_vab_used[vabid] == 2) {
        _spu_setInTransfer(0);
        _svm_vab_used[vabid] = 1;
        return vabid;
    }
    return -1;
}
#endif
