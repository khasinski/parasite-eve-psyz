#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_9", _SsSetNrpnVabAttr9);
#else
/* NRPN data entry: ADSR sustain rate, exponential */
void _SsSetNrpnVabAttr9(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.adsr2 = (vagatr.adsr2 & 0x403F) | 0x8000 | ((value & 0x7F) << 6);
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
