#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_0", _SsSetNrpnVabAttr0);
#else
/* NRPN data entry: tone priority */
void _SsSetNrpnVabAttr0(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.prior = value;
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
