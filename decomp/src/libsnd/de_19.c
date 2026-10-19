#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_19", _SsSetNrpnVabAttr19);
#else
/* NRPN data entry: delay time */
void _SsSetNrpnVabAttr19(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    SsUtSetReverbDelay(value);
}
#endif
