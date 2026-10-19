#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_11", _SsSetNrpnVabAttr11);
#else
/* NRPN data entry: ADSR release rate, exponential */
void _SsSetNrpnVabAttr11(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.adsr2 = (vagatr.adsr2 & 0xFFC0) | 0x20 | (value & 0x1F);
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
