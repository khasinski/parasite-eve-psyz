#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_15", _SsSetNrpnVabAttr15);
#else
/* NRPN data entry: reverb type */
void _SsSetNrpnVabAttr15(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    SsUtSetReverbType(value);
}
#endif
