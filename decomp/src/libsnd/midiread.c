#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midiread", _SsSeqPlay);

INCLUDE_ASM("asm/nonmatchings/libsnd/midiread", _SsSeqGetEof);

INCLUDE_ASM("asm/nonmatchings/libsnd/midiread", _SsGetSeqData);
#else
/* SEQ/SEP sequencer core, as in PSY-Q 4.x (the library Ridge Racer Type 4
 * links). delta_value counts down the time to the next event in units of
 * 1/10 tick; unk54 is the number of such units per SsSeqCalledTbyT call, or
 * with unk52 >= 0 (slow tempos) unk52 counts calls per unit. */
void _SsSeqPlay(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    int acc;

    if (score->delta_value - score->unk54 > 0) {
        if (score->unk52 > 0) {
            score->unk52--;
            return;
        }
        if (score->unk52 == 0) {
            score->unk52 = score->unk54;
            score->delta_value--;
        } else {
            score->delta_value -= score->unk54;
        }
        return;
    }
    if (score->unk54 >= score->delta_value) {
        acc = score->delta_value;
        do {
            do {
                _SsGetSeqData(seq_access_num, seq_num);
            } while (score->delta_value == 0);
            acc += score->delta_value;
        } while (acc < score->unk54);
        score->delta_value = acc - score->unk54;
    }
}

void _SsSeqGetEof(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    s8 count = score->unk21 + 1;
    u8* start;

    score->unk21 = count;
    start = (score->flags & SEQ_FLAG_400) ? score->loop_pos : score->read_pos;
    if ((s8)score->l_count == 0) {
        /* SSPLAY_INFINITY */
        score->unk88 = 0;
        score->unk1C = 0;
        score->delta_value = 0;
        score->seq_ptr = start;
    } else if (count < (s8)score->l_count) {
        score->unk88 = 0;
        score->unk1C = 0;
        score->delta_value = 0;
        score->seq_ptr = start;
        score->next_sep_pos = start;
    } else {
        score->flags &= ~SEQ_FLAG_1;
        score->flags &= ~SEQ_FLAG_8;
        score->flags &= ~SEQ_FLAG_2;
        score->flags |= SEQ_FLAG_200;
        score->flags |= SEQ_FLAG_4;
        score->play_mode = SSPLAY_PAUSE;
        score->next_sep_pos = start;
        if ((s8)score->unk22 != -1) {
            score->play_mode = SSPLAY_PAUSE;
            _SsSndNextSep((s8)score->unk22, (s8)score->unk23);
            _SsVmSeqKeyOff(seq_access_num | (seq_num << 8));
        }
        _SsVmSeqKeyOff(seq_access_num | (seq_num << 8));
        score->delta_value = score->unk54;
    }
}

typedef void (*SsEventFn)(short, short, unsigned char, unsigned char);
typedef void (*SsDataFn)(short, short, unsigned char);
typedef void (*SsBendFn)(short, short);

int _SsGetSeqData(short seq_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 c, note, vel, value;
    int ret = 0;

    c = *score->seq_ptr++;
    if ((score->flags & (SEQ_FLAG_400 | SEQ_FLAG_1)) ==
            (SEQ_FLAG_400 | SEQ_FLAG_1) &&
        score->unk10 && score->seq_ptr == score->unk10 + 1) {
        _SsSeqGetEof(seq_access_num, seq_num);
        return -1;
    }
    if (c & 0x80) {
        score->channel_idx = c & 0x0F;
        switch (c & 0xF0) {
        case 0x90:
            score->running_status = 0x90;
            note = *score->seq_ptr++;
            vel = *score->seq_ptr++;
            score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
            ((SsEventFn)SsFCALL.noteon)(seq_access_num, seq_num, note, vel);
            break;
        case 0xB0:
            score->running_status = 0xB0;
            value = *score->seq_ptr++;
            ((SsDataFn)SsFCALL.control[CC_NUMBER])(
                seq_access_num, seq_num, value);
            break;
        case 0xC0:
            score->running_status = 0xC0;
            value = *score->seq_ptr++;
            ((SsDataFn)SsFCALL.programchange)(seq_access_num, seq_num, value);
            break;
        case 0xE0:
            /* The LSB is skipped; the handler reads the MSB. */
            score->running_status = 0xE0;
            score->seq_ptr++;
            ((SsBendFn)SsFCALL.pitchbend)(seq_access_num, seq_num);
            break;
        case 0xF0:
            score->running_status = 0xFF;
            value = *score->seq_ptr++;
            if (value == 0x2F) {
                ret = 1;
                _SsSeqGetEof(seq_access_num, seq_num);
            } else {
                ((SsDataFn)SsFCALL.metaevent)(seq_access_num, seq_num, value);
            }
            break;
        }
    } else {
        switch (score->running_status) {
        case 0x90:
            vel = *score->seq_ptr++;
            score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
            ((SsEventFn)SsFCALL.noteon)(seq_access_num, seq_num, c, vel);
            break;
        case 0xB0:
            ((SsDataFn)SsFCALL.control[CC_NUMBER])(seq_access_num, seq_num, c);
            break;
        case 0xC0:
            ((SsDataFn)SsFCALL.programchange)(seq_access_num, seq_num, c);
            break;
        case 0xE0:
            ((SsBendFn)SsFCALL.pitchbend)(seq_access_num, seq_num);
            break;
        case 0xFF:
            if (c == 0x2F) {
                ret = 1;
                _SsSeqGetEof(seq_access_num, seq_num);
            } else {
                ((SsDataFn)SsFCALL.metaevent)(seq_access_num, seq_num, c);
            }
            break;
        }
    }
    return ret;
}
#endif
