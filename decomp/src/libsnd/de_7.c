#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_7", _SsSetNrpnVabAttr7);
#else
/* NRPN data entry: ADSR sustain level */
void _SsSetNrpnVabAttr7(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.adsr1 = (vagatr.adsr1 & 0xFFF0) | (value & 0x0F);
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
