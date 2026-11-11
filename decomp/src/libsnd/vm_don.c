#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_don", _SsVmDamperOn);
#else
/* The damper lengthens the release of voices keyed on from now. */
void _SsVmDamperOn(void) { _svm_damper = 2; }
#endif
