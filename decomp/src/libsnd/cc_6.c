#include "libsnd_private.h"
#include <string.h>

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_6", _SsContDataEntry);
#else
/* Data entry (CC#6): NRPN loop count, RPN pitch bend range, or an NRPN VAB
 * attribute edit dispatched through SsFCALL.ccentry. */
void _SsContDataEntry(short sep_access_num, short seq_num, unsigned char value) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    int ch = score->channel_idx;
    int prog = score->programs[ch];
    ProgAtr prog_attr;
    VagAtr tone_attr;
    int i;

    if (score->unk1E == 1 && score->unk1F == 0) {
        score->unk15 = value;
        score->unk1F = 1;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        return;
    }
    memset(&prog_attr, 0, sizeof(prog_attr));
    memset(&tone_attr, 0, sizeof(tone_attr));
    if (SsUtGetProgAtr(score->vab_id, prog, &prog_attr) != 0) {
        prog_attr.tones = 0;
    }
    if (score->unk1D == 2) {
        if (score->unk1C == 0 && score->unk1B == 0) {
            /* RPN 0/0: pitch bend sensitivity in semitones */
            for (i = 0; i < prog_attr.tones; i++) {
                if (SsUtGetVagAtr(score->vab_id, prog, i, &tone_attr) == 0) {
                    tone_attr.pbmin = value & 0x7F;
                    tone_attr.pbmax = value & 0x7F;
                    SsUtSetVagAtr(score->vab_id, prog, i, &tone_attr);
                }
            }
        }
        score->unk1D = 0;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        return;
    }
    if (score->unk18 == 2) {
        if (score->fn_idx < NUM_DE && SsFCALL.ccentry[score->fn_idx]) {
            if (score->unk19 == 0x10) {
                for (i = 0; i < prog_attr.tones; i++) {
                    ((SsFnNrpnAttr)SsFCALL.ccentry[score->fn_idx])(
                        score->vab_id, prog, i, tone_attr, score->fn_idx,
                        value);
                }
            } else {
                ((SsFnNrpnAttr)SsFCALL.ccentry[score->fn_idx])(score->vab_id, prog,
                                               score->unk19, tone_attr,
                                               score->fn_idx, value);
            }
        }
        score->unk18 = 0;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        return;
    }
    score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
}
#endif
