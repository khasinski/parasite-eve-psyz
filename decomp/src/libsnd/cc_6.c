#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/cc_6", _SsContDataEntry);
#else
typedef void (*SsNrpnFn)(short vabId, short prog, short tone, VagAtr vagatr,
                         short fn_idx, unsigned char data);

/* CC#6: data entry. It is the repeat count after an NRPN loop start, the
 * value of RPN 0 (pitch bend range) and RPN 1/2 (fine and coarse tune, which
 * the library computes but does not change), or the value of the VAB
 * attribute selected by NRPN (SsFCALL.ccentry). */
void _SsContDataEntry(short seq_access_num, short seq_num, unsigned char data) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 channel = score->channel_idx;
    ProgAtr progatr;
    VagAtr vagatr;
    int i;

    memset(&progatr, 0, sizeof(progatr));
    memset(&vagatr, 0, sizeof(vagatr));
    SsUtGetProgAtr(score->vab_id, score->programs[channel], &progatr);
    if (score->unk1C == 1 && score->unk15 == 0) {
        score->unk1D = data;
        score->unk1C = 0;
        score->unk15 = 1;
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
    } else if (score->unk1E == 2) {
        if (score->unk19 == 0) {
            for (i = 0; i < progatr.tones; i++) {
                SsUtGetVagAtr(
                    score->vab_id, score->programs[channel], i, &vagatr);
                switch (score->unk18) {
                case 0:
                    vagatr.pbmin = vagatr.pbmax = data & 0x7F;
                    break;
                case 1:
                case 2:
                    /* The tune offsets are multiples of 0x100 added to 8-bit
                     * fields: shift and center stay unchanged. */
                    break;
                }
                SsUtSetVagAtr(
                    score->vab_id, score->programs[channel], i, &vagatr);
            }
        }
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
        score->unk1E = 0;
    } else if (score->unk1F == 2) {
        /* The PS1 indexed the table with any NRPN LSB; only 0..19 exist. */
        if (score->fn_idx < LEN(SsFCALL.ccentry) &&
            SsFCALL.ccentry[score->fn_idx]) {
            ((SsNrpnFn)SsFCALL.ccentry[score->fn_idx])(
                score->vab_id, score->programs[channel],
                score->unk1B == 16 ? 0 : score->unk1B, vagatr, score->fn_idx,
                data);
        }
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
        score->unk1F = 0;
    } else {
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
    }
}
#endif
