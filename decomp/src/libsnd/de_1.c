#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_1", _SsSetNrpnVabAttr1);
#else
/* NRPN data entry: tone mode (4 = reverb) */
void _SsSetNrpnVabAttr1(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.mode = value;
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
