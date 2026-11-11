#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_no1", vmNoiseOn);
#else
/* Keys on a noise tone (VAG 0xFF): the noise clock follows the note. */
void vmNoiseOn(char voice) {
    struct SeqStruct* score;
    unsigned l, r, v;
    u8 vc = (u8)voice;
    u16 low, high;
    short i;

    v = _svm_cur.volume * (_svm_vh->mvol * 0x3FFF) / 16129;
    v = v * _svm_cur.mvol * _svm_cur.tone_vol / 16129;
    l = r = v;
    if (_svm_cur.seq_sep_no != 0x21) {
        score = &_ss_score[_svm_cur.seq_sep_no & 0xFF]
                          [(_svm_cur.seq_sep_no & 0xFF00) >> 8];
        l = v * score->voll / 127;
        r = v * score->volr / 127;
    }
    if ((u8)_svm_cur.tone_pan < 0x40) {
        r = r * (u8)_svm_cur.tone_pan / 63;
    } else {
        l = l * (0x7F - (u8)_svm_cur.tone_pan) / 63;
    }
    if ((u8)_svm_cur.mpan < 0x40) {
        r = r * (u8)_svm_cur.mpan / 63;
    } else {
        l = l * (0x7F - (u8)_svm_cur.mpan) / 63;
    }
    if ((u8)_svm_cur.pan < 0x40) {
        r = r * (u8)_svm_cur.pan / 63;
    } else {
        l = l * (0x7F - (u8)_svm_cur.pan) / 63;
    }
    if (_svm_stereo_mono == 1) {
        if (l < r) {
            l = r;
        } else {
            r = l;
        }
    }
    if (_svm_cur.seq_sep_no != 0x21) {
        l = l * l / 16383;
        r = r * r / 16383;
    }
    SpuSetNoiseClock((_svm_cur.note - _svm_cur.tone_center) & 0x3F);
    _svm_sreg_buf[vc].volume.right = r;
    _svm_sreg_buf[vc].volume.left = l;
    _svm_sreg_dirty[vc] |= 3;
    if (vc < 16) {
        low = 1 << vc;
        high = 0;
    } else {
        low = 0;
        high = 1 << (vc - 16);
    }
    _svm_voice[vc].unk04 = 0xA;
    /* Only one voice plays noise: release the noise mark of the others. */
    for (i = 0; i < _SsVmMaxVoice; i++) {
        _svm_voice[i].unk1b &= 1;
    }
    _svm_voice[vc].unk1b = 2;
    _svm_okon1 |= low;
    _svm_okon2 |= high;
    _svm_okof1 &= ~_svm_okon1;
    _svm_okof2 &= ~_svm_okon2;
    if (_svm_cur.tone_mode & 4) {
        _svm_orev1 |= low;
        _svm_orev2 |= high;
    } else {
        _svm_orev1 &= ~low;
        _svm_orev2 &= ~high;
    }
    _svm_onos1 = low;
    _svm_onos2 = high;
}
#endif
