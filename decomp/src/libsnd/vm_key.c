#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmKeyOn);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmKeyOff);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmSeKeyOn);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", _SsVmSeKeyOff);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_key", KeyOnCheck);
#else
/* Keys on every tone of the program whose key range holds the note.
 * seq_sep_no 0x21 marks a sound effect; otherwise the low byte is the
 * access number and the high byte the SEP track, and the velocity is scaled
 * by the volume of the current channel. Returns the mask of voices keyed on,
 * or -1. */
int _SsVmKeyOn(short seq_sep_no, short vabId, short prog, short note,
               short voll, short pan) {
    unsigned char vags[128];
    unsigned char tones[128];
    unsigned short vel = voll;
    unsigned short pn = pan;
    struct SeqStruct* score = NULL;
    unsigned char n, i;
    unsigned short tone;
    short voice;
    int ret = 0;

    if (_SsVmVSetUp(vabId, prog) != 0) {
        return -1;
    }
    _svm_cur.seq_sep_no = seq_sep_no;
    _svm_cur.note = note;
    _svm_cur.fine = 0;
    if (seq_sep_no == 0x21) {
        _svm_cur.volume = vel;
    } else {
        score = &_ss_score[seq_sep_no & 0xFF][(seq_sep_no & 0xFF00) >> 8];
        _svm_cur.volume = vel * score->vol[score->channel_idx] / 127;
    }
    _svm_cur.pan = pn;
    _svm_cur.mvol = _svm_pg[prog].mvol;
    _svm_cur.mpan = _svm_pg[prog].mpan;
    _svm_cur.prog_tones = _svm_pg[prog].tones;
    if (_svm_cur.fake_program >= _svm_vh->ps) {
        return -1;
    }
    if (vel == 0) {
        return _SsVmKeyOff(seq_sep_no, vabId, prog, note);
    }
    n = _SsVmSelectToneAndVag(tones, vags);
    for (i = 0; i < n; i++) {
        _svm_cur.tone_vag_idx = vags[i];
        _svm_cur.tone = tones[i];
        tone = _svm_cur.fake_program * 16 + _svm_cur.tone;
        _svm_cur.tone_prior = _svm_tn[tone].prior;
        _svm_cur.tone_vol = _svm_tn[tone].vol;
        _svm_cur.tone_pan = _svm_tn[tone].pan;
        _svm_cur.tone_center = _svm_tn[tone].center;
        _svm_cur.tone_shift = _svm_tn[tone].shift;
        _svm_cur.tone_mode = _svm_tn[tone].mode;
        voice = (unsigned char)_SsVmAlloc(0);
        _svm_cur.voice = voice;
        if (voice < _SsVmMaxVoice) {
            _svm_voice[voice].unk1b = 1;
            _svm_voice[voice].unk2 = 0;
            _svm_voice[voice].seq_sep_no = seq_sep_no;
            _svm_voice[voice].vabId = _svm_cur.vabId;
            _svm_voice[voice].fake_program = _svm_cur.fake_program;
            _svm_voice[voice].prog = prog;
            if (seq_sep_no != 0x21) {
                _svm_voice[voice].voll1 = vel;
                _svm_voice[voice].channel = score->channel_idx;
            }
            _svm_voice[voice].pan = pn;
            _svm_voice[voice].tone = _svm_cur.tone;
            _svm_voice[voice].note = note;
            _svm_voice[voice].priority = _svm_cur.tone_prior;
            _svm_voice[voice].vag_idx = _svm_cur.tone_vag_idx;
            _SsVmDoAllocate();
            if (_svm_cur.tone_vag_idx == 0xFF) {
                vmNoiseOn(voice);
            } else {
                _SsVmKeyOnNow(n, note2pitch());
            }
            ret |= 1 << voice;
        } else {
            ret = -1;
        }
    }
    return ret;
}

int _SsVmKeyOff(short seq_sep_no, short vabId, short prog, short note) {
    u8 i;
    int count = 0;

    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_svm_voice[i].note == note && _svm_voice[i].prog == prog &&
            _svm_voice[i].seq_sep_no == seq_sep_no &&
            _svm_voice[i].vabId == vabId) {
            if (_svm_voice[i].vag_idx == 0xFF) {
                count++;
                vmNoiseOff(i);
            } else {
                _svm_cur.voice = i;
                _SsVmKeyOffNow(0);
                count++;
            }
        }
    }
    return count;
}

void _SsVmSeKeyOn(
    short vabId, short prog, unsigned short note, unsigned short fine,
    unsigned short voll, unsigned short volr) {
    unsigned short vol, pan;

    (void)fine;
    if (voll == volr) {
        pan = 0x40;
        vol = voll;
    } else if (volr < voll) {
        pan = (volr << 6) / voll;
        vol = voll;
    } else {
        vol = volr;
        pan = 0x7F - (voll << 6) / volr;
    }
    _SsVmKeyOn(0x21, vabId, prog, note, vol, pan);
}

int _SsVmSeKeyOff(short vabId, short prog, unsigned short note) {
    return _SsVmKeyOff(0x21, vabId, prog, note);
}

void KeyOnCheck(void) {}
#endif
