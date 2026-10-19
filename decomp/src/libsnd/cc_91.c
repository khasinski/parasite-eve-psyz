#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_91", _SsContExternal);
#else
/* External effect depth (CC#91) drives the reverb depth. */
void _SsContExternal(short sep_access_num, short seq_num, unsigned char depth) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];

    SsUtSetReverbDepth(depth, depth);
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
