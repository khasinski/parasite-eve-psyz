#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_0", _SsSetNrpnVabAttr0);
#else
void _SsSetNrpnVabAttr0(short vabId, short prog, short tone, VagAtr vagatr,
                        short fn_idx, unsigned char data) {
    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    vagatr.prior = data;
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
}
#endif
