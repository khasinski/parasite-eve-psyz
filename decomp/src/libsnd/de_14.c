#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_14", _SsSetNrpnVabAttr14);
#else
void _SsSetNrpnVabAttr14(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    struct Unk adsr;

    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    _SsUtResolveADSR(vagatr.adsr1, vagatr.adsr2, &adsr);
    vagatr.porW = data;
    _SsUtBuildADSR(&adsr, &vagatr.adsr1, &vagatr.adsr2);
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
}
#endif
