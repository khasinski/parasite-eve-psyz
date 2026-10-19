#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_98", _SsContNrpn1);
#else
/* NRPN LSB (CC#98). Score fields:
 *   unk19  NRPN MSB: tone number (16 = all tones) or 20/30/40 markers
 *   fn_idx NRPN LSB: VAB attribute selector (SsFCALL.ccentry index)
 *   unk18  NRPN bytes received since the last data entry
 *   unk1E  loop start seen, unk1F loop count received, unk15 loops left */
void _SsContNrpn1(short sep_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    SndSsMarkCallbackProc cb;

    if (score->unk1E == 1 && score->unk1F == 0) {
        score->unk15 = value;
        score->unk1F = 1;
    } else if (score->unk19 != 30 && score->unk19 != 20) {
        score->fn_idx = value;
        score->unk18++;
    }
    if (score->unk19 == 40) {
        cb = _SsMarkCallback[sep_access_num][seq_num];
        if (cb != NULL) {
            cb(sep_access_num, seq_num, value);
        }
    }
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
