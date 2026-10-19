#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_14", _SsSetNrpnVabAttr14);
#else
/* NRPN data entry: portamento depth (stored, portamento is not implemented) */
void _SsSetNrpnVabAttr14(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.porW = value;
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
