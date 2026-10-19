#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_17", _SsSetNrpnVabAttr17);
#else
/* NRPN data entry: echo feedback */
void _SsSetNrpnVabAttr17(short vab_id, short prog, short tone, VagAtr vagatr,
                        short fn_no, unsigned char value) {
    SsUtSetReverbFeedback(value);
}
#endif
