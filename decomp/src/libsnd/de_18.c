#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_18", _SsSetNrpnVabAttr18);
#else
void _SsSetNrpnVabAttr18(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    (void)vabId, (void)prog, (void)tone, (void)vagatr, (void)fn_idx;
    SsUtSetReverbDelay(data);
}
#endif
