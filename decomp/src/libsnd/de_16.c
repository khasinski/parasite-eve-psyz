#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_16", _SsSetNrpnVabAttr16);
#else
void _SsSetNrpnVabAttr16(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    (void)vabId, (void)prog, (void)tone, (void)vagatr, (void)fn_idx;
    SsUtSetReverbDepth(data, data);
}
#endif
