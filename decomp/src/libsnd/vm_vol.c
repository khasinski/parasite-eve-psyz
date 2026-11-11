#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_vol", _SsVmSetVol);
#else
/* Pans the panned volume of a voice: pan < 64 lowers the right channel,
 * otherwise the left one. */
static void Pan(unsigned* left, unsigned* right, unsigned pan) {
    if (pan < 0x40) {
        *right = *right * pan / 63;
    } else {
        *left = *left * (0x7F - pan) / 63;
    }
}

/* Applies a channel volume and pan (CC#7/#10/#11) to the sounding voices of
 * the program in this sequence. Returns the number of voices changed. */
int _SsVmSetVol(
    short seq_sep_no, short vabId, short prog, short voll, short pan) {
    struct SeqStruct* score =
        &_ss_score[seq_sep_no & 0xFF][(seq_sep_no & 0xFF00) >> 8];
    unsigned short vol = voll;
    unsigned short pn = pan;
    unsigned l, r, v;
    u16 tone;
    short i;
    int count = 0;

    _SsVmVSetUp(vabId, prog);
    _svm_cur.seq_sep_no = seq_sep_no;
    if (pn == 0) {
        pn = 1;
    }
    if (vol == 0) {
        vol = 1;
    }
    for (i = 0; i < _SsVmMaxVoice; i++) {
        if (_svm_voice[i].seq_sep_no != seq_sep_no ||
            _svm_voice[i].prog != prog || _svm_voice[i].vabId != vabId) {
            continue;
        }
        if (score->vol[score->channel_idx] != vol &&
            score->vol[score->channel_idx] == 0) {
            score->vol[score->channel_idx] = 1;
        }
        l = (_svm_voice[i].voll1 * vol / 127) * (_svm_vh->mvol * 0x3FFF) /
            16129;
        tone = _svm_voice[i].fake_program * 16 + _svm_voice[i].tone;
        v = l * _svm_pg[prog].mvol * _svm_tn[tone].vol / 16129;
        l = v * score->voll / 127;
        r = v * score->volr / 127;
        Pan(&l, &r, _svm_tn[tone].pan);
        Pan(&l, &r, _svm_pg[_svm_voice[i].prog].mpan);
        Pan(&l, &r, pn);
        if (_svm_stereo_mono == 1) {
            if (l < r) {
                l = r;
            } else {
                r = l;
            }
        }
        _svm_sreg_buf[i].volume.left = l * l / 16383;
        _svm_sreg_buf[i].volume.right = r * r / 16383;
        count++;
        _svm_sreg_dirty[i] |= 3;
    }
    return count;
}
#endif
