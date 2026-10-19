#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_3", _SsSetNrpnVabAttr3);
#else
/* NRPN data entry: highest note */
void _SsSetNrpnVabAttr3(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.max = value;
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
