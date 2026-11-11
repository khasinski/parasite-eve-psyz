#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_19", _SsSetNrpnVabAttr19);
#else
void _SsSetNrpnVabAttr19(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    /* Delay time: the library sets the reverb delay like attribute 18. */
    (void)vabId, (void)prog, (void)tone, (void)vagatr, (void)fn_idx;
    SsUtSetReverbDelay(data);
}
#endif
