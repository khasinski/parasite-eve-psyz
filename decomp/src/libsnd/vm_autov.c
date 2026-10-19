#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_autov", SeAutoVol);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_autov", SetAutoVol);
#else
/* Set up an automatic volume sweep from start_vol to end_vol over delta_time
 * VSync ticks; SetAutoVol advances it from _SsVmFlush.
 *   unk1e step, unk20 ticks per step, unk22 countdown */
void SeAutoVol(short voice, short start_vol, short end_vol, short delta_time) {
    int diff = end_vol - start_vol;
    int adiff = diff < 0 ? -diff : diff;

    _svm_voice[voice].start_vol = start_vol;
    _svm_voice[voice].end_vol = end_vol;
    if (diff == 0 || delta_time <= 0) {
        _svm_voice[voice].start_vol = start_vol;
        _svm_voice[voice].unk1e = diff;
        _svm_voice[voice].unk20 = 0;
        _svm_voice[voice].unk22 = 0;
        _svm_voice[voice].auto_vol = diff != 0;
        return;
    }
    if (adiff < delta_time) {
        _svm_voice[voice].unk1e = diff > 0 ? 1 : -1;
        _svm_voice[voice].unk20 = delta_time / adiff;
    } else {
        _svm_voice[voice].unk1e = diff / delta_time;
        _svm_voice[voice].unk20 = 0;
    }
    _svm_voice[voice].unk22 = _svm_voice[voice].unk20;
    _svm_voice[voice].auto_vol = 1;
}

void SetAutoVol(short voice) {
    unsigned int voll, volr, v;
    int tn;
    u8 p;

    if (_svm_voice[voice].unk22-- > 0) {
        return;
    }
    _svm_voice[voice].start_vol += _svm_voice[voice].unk1e;
    if (_svm_voice[voice].unk1e > 0) {
        if (_svm_voice[voice].start_vol >= _svm_voice[voice].end_vol) {
            _svm_voice[voice].start_vol = _svm_voice[voice].end_vol;
            _svm_voice[voice].auto_vol = 0;
        }
    } else if (_svm_voice[voice].unk1e < 0) {
        if (_svm_voice[voice].start_vol <= _svm_voice[voice].end_vol) {
            _svm_voice[voice].start_vol = _svm_voice[voice].end_vol;
            _svm_voice[voice].auto_vol = 0;
        }
    } else {
        _svm_voice[voice].auto_vol = 0;
    }
    if (_svm_voice[voice].start_vol < 0) {
        _svm_voice[voice].start_vol = 0;
    }
    if (_svm_voice[voice].start_vol > 127) {
        _svm_voice[voice].start_vol = 127;
    }
    if (_svm_vh == NULL || _svm_pg == NULL || _svm_tn == NULL) {
        return;
    }
    tn = _svm_voice[voice].fake_program * 16 + _svm_voice[voice].tone;
    v = _svm_voice[voice].start_vol * (_svm_vh->mvol * 0x3FFF) / 0x3F01;
    v = v * _svm_pg[_svm_voice[voice].prog].mvol * _svm_tn[tn].vol / 0x3F01;
    voll = volr = v;
    p = _svm_tn[tn].pan;
    if (p < 64) {
        volr = volr * p / 63;
    } else {
        voll = voll * (127 - p) / 63;
    }
    p = _svm_pg[_svm_voice[voice].prog].mpan;
    if (p < 64) {
        volr = volr * p / 63;
    } else {
        voll = voll * (127 - p) / 63;
    }
    p = _svm_voice[voice].pan;
    if (p < 64) {
        volr = volr * p / 63;
    } else {
        voll = voll * (127 - p) / 63;
    }
    if (_svm_stereo_mono == 1) {
        if (voll < volr) {
            voll = volr;
        } else {
            volr = voll;
        }
    }
    _svm_voice[voice].unk22 = _svm_voice[voice].unk20;
    ((short*)_svm_sreg_buf)[voice * 8 + 0] = voll * voll / 0x3FFF;
    ((short*)_svm_sreg_buf)[voice * 8 + 1] = volr * volr / 0x3FFF;
    _svm_sreg_dirty[voice] |= 3;
}
#endif
