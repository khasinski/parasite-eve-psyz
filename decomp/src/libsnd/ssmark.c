#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/ssmark", SsSetMarkCallback);

#else
void SsSetMarkCallback(
    short access_num, short seq_num, SsMarkCallbackProc proc) {
    if (access_num < 0 || access_num >= 32 || seq_num < 0 || seq_num >= 16) {
        return;
    }
    _SsMarkCallback[access_num][seq_num] = (SndSsMarkCallbackProc)proc;
}
#endif
