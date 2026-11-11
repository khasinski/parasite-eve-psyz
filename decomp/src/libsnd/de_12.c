#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_12", _SsSetNrpnVabAttr12);
#else
void _SsSetNrpnVabAttr12(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    struct Unk adsr;

    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    _SsUtResolveADSR(vagatr.adsr1, vagatr.adsr2, &adsr);
    if ((u8)(data - 1) < 0x3F) {
        adsr.sustain_decrease = 0;
    } else if ((u8)(data - 0x40) < 0x40) {
        adsr.sustain_decrease = 1;
    }
    _SsUtBuildADSR(&adsr, &vagatr.adsr1, &vagatr.adsr2);
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
}
#endif
