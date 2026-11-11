#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_3", _SsSetNrpnVabAttr3);
#else
void _SsSetNrpnVabAttr3(short vabId, short prog, short tone, VagAtr vagatr,
                        short fn_idx, unsigned char data) {
    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    vagatr.max = data;
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
}
#endif
