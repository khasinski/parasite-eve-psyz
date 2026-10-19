#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_16", _SsSetNrpnVabAttr16);
#else
/* NRPN data entry: reverb depth */
void _SsSetNrpnVabAttr16(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    SsUtSetReverbDepth(value, value);
}
#endif
