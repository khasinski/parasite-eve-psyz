#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmKeyOn);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmKeyOff);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmSeKeyOn);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmSeKeyOff);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", KeyOnCheck);

#else
#include <stdio.h>
#include <stdlib.h>

/* PSYZ_SND_TRACE=1 logs every sequencer/SE key on to stderr. */
static int trace_keyon(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char* env = getenv("PSYZ_SND_TRACE");
        enabled = env != NULL && env[0] != '\0' && env[0] != '0';
    }
    return enabled;
}

/* Key on every tone of vabId/prog whose key range covers note. voll is the
 * note velocity (scaled by the channel volume for sequences), pan the channel
 * pan. Returns a mask of the allocated voices, or -1. */
short _SsVmKeyOn(int seq_sep_no, short vabId, short prog, short note,
                 short voll, short pan) {
    struct SeqStruct* score = NULL;
    u8 vag_list[128];
    u8 tone_list[128];
    int count;
    int i;
    int tn;
    short voice;
    short ret = 0;

    if (trace_keyon()) {
        fprintf(stderr, "[libsnd] key on seq_sep=%04x vab=%d prog=%d note=%d vel=%d\n",
                seq_sep_no & 0xFFFF, vabId, prog, note, voll);
    }
    if (seq_sep_no != 0x21) {
        score = _SsScoreOf(seq_sep_no);
        if (score == NULL) {
            return -1;
        }
    }
    if (_SsVmVSetUp(vabId, prog) != 0) {
        return -1;
    }
    _svm_cur.seq_sep_no = seq_sep_no;
    _svm_cur.note = note;
    _svm_cur.fine = 0;
    if (score == NULL) {
        _svm_cur.volume = voll;
    } else {
        _svm_cur.volume = voll * score->vol[score->channel_idx] / 127;
    }
    _svm_cur.pan = pan;
    _svm_cur.mvol = _svm_pg[prog].mvol;
    _svm_cur.mpan = _svm_pg[prog].mpan;
    _svm_cur.prog_tones = _svm_pg[prog].tones;
    if (voll == 0) {
        return _SsVmKeyOff(seq_sep_no, vabId, prog, note);
    }

    count = _SsVmSelectToneAndVag(vag_list, tone_list);
    for (i = 0; i < count; i++) {
        _svm_cur.tone_vag_idx = vag_list[i];
        _svm_cur.tone = tone_list[i];
        tn = _svm_cur.fake_program * 16 + _svm_cur.tone;
        _svm_cur.tone_prior = _svm_tn[tn].prior;
        _svm_cur.tone_vol = _svm_tn[tn].vol;
        _svm_cur.tone_pan = _svm_tn[tn].pan;
        _svm_cur.tone_center = _svm_tn[tn].center;
        _svm_cur.tone_shift = _svm_tn[tn].shift;
        _svm_cur.tone_min = _svm_tn[tn].min;
        _svm_cur.tone_max = _svm_tn[tn].max;
        _svm_cur.tone_mode = _svm_tn[tn].mode;

        voice = _SsVmAlloc(_svm_cur.tone_vag_idx);
        if (voice >= _SsVmMaxVoice) {
            ret = -1;
            continue;
        }
        _svm_cur.voice = voice;
        _svm_voice[voice].unk1b = 1;
        _svm_voice[voice].unk2 = 0;
        _svm_voice[voice].seq_sep_no = seq_sep_no;
        _svm_voice[voice].vabId = vabId;
        _svm_voice[voice].fake_program = _svm_cur.fake_program;
        _svm_voice[voice].prog = prog;
        if (score != NULL) {
            _svm_voice[voice].voll1 = voll;
        }
        _svm_voice[voice].pan = pan;
        _svm_voice[voice].tone = _svm_cur.tone;
        _svm_voice[voice].note = note;
        _svm_voice[voice].priority = _svm_cur.tone_prior;
        _svm_voice[voice].vag_idx = _svm_cur.tone_vag_idx;
        _SsVmDoAllocate();
        if (_svm_cur.tone_vag_idx == 0xFF) {
            vmNoiseOn(voice);
        } else {
            _SsVmKeyOnNow(count, note2pitch());
        }
        if (ret >= 0) {
            ret |= 1 << voice;
        }
    }
    return ret;
}

/* Release every voice playing note of vabId/prog for this sequence. */
short _SsVmKeyOff(int seq_sep_no, short vabId, short prog, short note) {
    int i;
    short count = 0;

    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_svm_voice[i].note == note && _svm_voice[i].prog == prog &&
            _svm_voice[i].seq_sep_no == (short)seq_sep_no &&
            _svm_voice[i].vabId == vabId) {
            if (_svm_voice[i].vag_idx == 0xFF) {
                vmNoiseOff(i);
            } else {
                _svm_cur.voice = i;
                _SsVmKeyOffNow(0);
            }
            count++;
        }
    }
    return count;
}
#endif
