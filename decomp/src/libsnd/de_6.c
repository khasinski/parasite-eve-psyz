#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_6", _SsSetNrpnVabAttr6);
#else
/* NRPN data entry: ADSR decay rate */
void _SsSetNrpnVabAttr6(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.adsr1 = (vagatr.adsr1 & 0xFF0F) | ((value & 0x0F) << 4);
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
