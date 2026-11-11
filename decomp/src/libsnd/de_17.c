#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/de_17", _SsSetNrpnVabAttr17);
#else
void _SsSetNrpnVabAttr17(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data) {
    (void)vabId, (void)prog, (void)tone, (void)vagatr, (void)fn_idx;
    SsUtSetReverbFeedback(data);
}
#endif
