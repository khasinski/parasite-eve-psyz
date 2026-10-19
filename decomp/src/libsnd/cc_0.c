#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_0", _SsContBankChange);
#else
/* Bank select (CC#0). The SEP is bound to the VAB given at open time, so the
 * value (stashed in unk47 by _SsSetControlChange) is not applied. */
void _SsContBankChange(short sep_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
