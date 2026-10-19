#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_2", _SsSetNrpnVabAttr2);
#else
/* NRPN data entry: lowest note */
void _SsSetNrpnVabAttr2(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.min = value;
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
