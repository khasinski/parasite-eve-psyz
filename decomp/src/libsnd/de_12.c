#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_12", _SsSetNrpnVabAttr12);
#else
/* NRPN data entry: ADSR sustain direction (>= 64 decreases) */
void _SsSetNrpnVabAttr12(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    if (value >= 64) {
        vagatr.adsr2 |= 0x4000;
    } else {
        vagatr.adsr2 &= ~0x4000;
    }
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
