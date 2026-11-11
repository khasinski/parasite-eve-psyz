#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_2", _SsSetNrpnVabAttr2);
#else
void _SsSetNrpnVabAttr2(short vabId, short prog, short tone, VagAtr vagatr,
                        short fn_idx, unsigned char data) {
    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    vagatr.min = data;
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
}
#endif
