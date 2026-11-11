#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_15", _SsSetNrpnVabAttr15);
#else
void _SsSetNrpnVabAttr15(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    (void)vabId, (void)prog, (void)tone, (void)vagatr, (void)fn_idx;
    SsUtSetReverbType(data);
}
#endif
