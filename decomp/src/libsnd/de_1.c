#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_1", _SsSetNrpnVabAttr1);
#else
void _SsSetNrpnVabAttr1(short vabId, short prog, short tone, VagAtr vagatr,
                        short fn_idx, unsigned char data) {
    (void)fn_idx;
    SsUtGetVagAtr(vabId, prog, tone, &vagatr);
    vagatr.mode = data;
    SsUtSetVagAtr(vabId, prog, tone, &vagatr);
    if (data == 0) {
        SsUtReverbOff();
    } else if (data == 4) {
        SsUtReverbOn();
    }
}
#endif
