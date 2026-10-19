#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_10", _SsSetNrpnVabAttr10);
#else
/* NRPN data entry: ADSR release rate, linear */
void _SsSetNrpnVabAttr10(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.adsr2 = (vagatr.adsr2 & 0xFFC0) | (value & 0x1F);
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
