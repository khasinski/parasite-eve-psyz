#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midiread", _SsSeqPlay);

INCLUDE_ASM("asm/nonmatchings/libsnd/midiread", _SsSeqGetEof);

INCLUDE_ASM("asm/nonmatchings/libsnd/midiread", _SsGetSeqData);
#else
/* Reconstructed from the PsyQ libsnd sequencer (seqread.o) semantics.
 *
 * Score fields used by the reader:
 *   seq_ptr        current read pointer
 *   read_pos       first event after the header (restart point)
 *   loop_pos       NRPN loop start (CC99 = 20)
 *   running_status last MIDI status byte (0xFF for meta events)
 *   channel_idx    MIDI channel of the last status byte
 *   delta_value    ticks (x10) left until the next event
 *   unk52          > 0: VSyncs per tick countdown, -1: several ticks per VSync
 *   unk54          ticks per VSync (or VSyncs per tick when unk52 > 0)
 */

/* Upper bound of events consumed in one tick; corrupt data (an endless run of
 * zero deltas) must not hang the host. */
#define MAX_EVENTS_PER_TICK 4096

void _SsSeqPlay(short sep_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    int ticks = score->unk54;
    int remain = score->delta_value - ticks;
    int sum;
    int guard;

    if (remain > 0) {
        if (score->unk52 > 0) {
            score->unk52--;
        } else if (score->unk52 == 0) {
            score->unk52 = score->unk54;
            score->delta_value--;
        } else {
            score->delta_value = remain;
        }
        return;
    }

    sum = score->delta_value;
    guard = 0;
    do {
        do {
            _SsGetSeqData(sep_access_num, seq_num);
            if (++guard >= MAX_EVENTS_PER_TICK) {
                score->delta_value = 0;
                return;
            }
        } while (score->delta_value == 0);
        sum += score->delta_value;
    } while (sum < score->unk54);
    score->delta_value = sum - score->unk54;
}

/* End of track (meta event 0x2F): loop back or stop. */
void _SsSeqGetEof(short sep_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    short seq_sep_no = sep_access_num | (seq_num << 8);

    score->unk21++;
    if (score->l_count == 0 || score->unk21 < score->l_count) {
        /* SSPLAY_INFINITY or loops left: restart from the top */
        score->unk88 = 0;
        score->unk1E = 0;
        score->unk1F = 0;
        score->delta_value = 0;
        score->seq_ptr = score->read_pos;
        score->loop_pos = score->read_pos;
        return;
    }
    score->flags &= ~(SEQ_FLAG_1 | SEQ_FLAG_8 | SEQ_FLAG_2);
    score->flags |= SEQ_FLAG_200 | SEQ_FLAG_4;
    score->play_mode = SSPLAY_PAUSE;
    score->loop_pos = score->read_pos;
    if (score->unk22 != 0xFF) {
        _SsSndNextSep(score->unk22, score->unk23);
    }
    _SsVmSeqKeyOff(seq_sep_no);
    score->delta_value = score->unk54;
}

/* Read one MIDI event (with running status) and dispatch it through SsFCALL.
 * Every handler leaves the following delta time in score->delta_value. */
void _SsGetSeqData(short sep_access_num, short seq_num) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    unsigned char status;
    unsigned char data;
    unsigned char vel;

    status = *score->seq_ptr++;
    if (status & 0x80) {
        score->channel_idx = status & 0x0F;
        switch (status & 0xF0) {
        case 0x80: /* note off */
            score->running_status = 0x80;
            data = *score->seq_ptr++;
            score->seq_ptr++;
            score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
            ((SsFnNoteOn)SsFCALL.noteon)(sep_access_num, seq_num, data, 0);
            break;
        case 0x90: /* note on */
            score->running_status = 0x90;
            data = *score->seq_ptr++;
            vel = *score->seq_ptr++;
            score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
            ((SsFnNoteOn)SsFCALL.noteon)(sep_access_num, seq_num, data, vel);
            break;
        case 0xA0: /* polyphonic aftertouch: unsupported, skip */
            score->running_status = 0xA0;
            score->seq_ptr += 2;
            score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
            break;
        case 0xB0: /* control change */
            score->running_status = 0xB0;
            data = *score->seq_ptr++;
            ((SsFnEvent)SsFCALL.control[CC_NUMBER])(sep_access_num, seq_num, data);
            break;
        case 0xC0: /* program change */
            score->running_status = 0xC0;
            data = *score->seq_ptr++;
            ((SsFnEvent)SsFCALL.programchange)(sep_access_num, seq_num, data);
            break;
        case 0xD0: /* channel aftertouch: unsupported, skip */
            score->running_status = 0xD0;
            score->seq_ptr++;
            score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
            break;
        case 0xE0: /* pitch bend: LSB is ignored, the handler reads the MSB */
            score->running_status = 0xE0;
            score->seq_ptr++;
            ((SsFnEvent2)SsFCALL.pitchbend)(sep_access_num, seq_num);
            break;
        case 0xF0: /* meta event */
            score->running_status = 0xFF;
            data = *score->seq_ptr++;
            ((SsFnEvent)SsFCALL.metaevent)(sep_access_num, seq_num, data);
            break;
        }
        return;
    }

    /* running status: the byte just read is the first data byte */
    switch (score->running_status) {
    case 0x80:
        score->seq_ptr++;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        ((SsFnNoteOn)SsFCALL.noteon)(sep_access_num, seq_num, status, 0);
        break;
    case 0x90:
        vel = *score->seq_ptr++;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        ((SsFnNoteOn)SsFCALL.noteon)(sep_access_num, seq_num, status, vel);
        break;
    case 0xA0:
        score->seq_ptr++;
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        break;
    case 0xB0:
        ((SsFnEvent)SsFCALL.control[CC_NUMBER])(sep_access_num, seq_num, status);
        break;
    case 0xC0:
        ((SsFnEvent)SsFCALL.programchange)(sep_access_num, seq_num, status);
        break;
    case 0xD0:
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        break;
    case 0xE0:
        ((SsFnEvent2)SsFCALL.pitchbend)(sep_access_num, seq_num);
        break;
    case 0xFF:
        ((SsFnEvent)SsFCALL.metaevent)(sep_access_num, seq_num, status);
        break;
    default:
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        break;
    }
}
#endif
