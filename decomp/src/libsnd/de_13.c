#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_13", _SsSetNrpnVabAttr13);
#else
/* NRPN data entry: vibrato time (stored, vibrato is not implemented) */
void _SsSetNrpnVabAttr13(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    if (SsUtGetVagAtr(vab_id, prog, tone, &vagatr) != 0) {
        return;
    }
    vagatr.vibT = value;
    SsUtSetVagAtr(vab_id, prog, tone, &vagatr);
}
#endif
