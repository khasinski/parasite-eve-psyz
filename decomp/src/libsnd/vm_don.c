#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_don", _SsVmDamperOn);

#else
void _SsVmDamperOn(void) { _svm_damper = 2; }
#endif
