#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_11", _SsSetNrpnVabAttr11);
#else
void _SsSetNrpnVabAttr11(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    struct Unk adsr;

    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    _SsUtResolveADSR(vagatr.adsr1, vagatr.adsr2, &adsr);
    /* The PS1 left the other fields of adsr uninitialized here and in
     * attributes 12 to 14; the host keeps the tone's own values. */
    adsr.release_exp = 1;
    adsr.release_rate = data;
    _SsUtBuildADSR(&adsr, &vagatr.adsr1, &vagatr.adsr2);
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
}
#endif
