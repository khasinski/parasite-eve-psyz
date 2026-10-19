#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_4", _SsSetNrpnVabAttr4);
#else
/* NRPN data entry: ADSR attack rate, linear */
void _SsSetNrpnVabAttr4(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.adsr1 = (vagatr.adsr1 & 0x00FF) | ((value & 0x7F) << 8);
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
