#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_18", _SsSetNrpnVabAttr18);
#else
/* NRPN data entry: echo delay time */
void _SsSetNrpnVabAttr18(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    SsUtSetReverbDelay(value);
}
#endif
