#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_6", _SsSetNrpnVabAttr6);
#else
void _SsSetNrpnVabAttr6(short vabId, short prog, short tone, VagAtr vagatr,
                        short fn_idx, unsigned char data) {
    struct Unk adsr;

    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    _SsUtResolveADSR(vagatr.adsr1, vagatr.adsr2, &adsr);
    adsr.decay_rate = data;
    _SsUtBuildADSR(&adsr, &vagatr.adsr1, &vagatr.adsr2);
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
}
#endif
