#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_vol", _SsVmSetVol);

#else
/* Recompute the stereo volume of the voices of seq_sep_no/vabId/prog for a
 * new channel volume and pan (CC#7/CC#10/CC#11). */
short _SsVmSetVol(short seq_sep_no, short vabId, short prog, short vol,
                  short pan) {
    struct SeqStruct* score = _SsScoreOf(seq_sep_no);
    unsigned int voll, volr, v;
    int tn;
    u8 p;
    int i;
    short count = 0;

    if (score == NULL || _SsVmVSetUp(vabId, prog) != 0) {
        return 0;
    }
    _svm_cur.seq_sep_no = seq_sep_no;
    if ((u16)pan == 0) {
        pan = 1;
    }
    if ((u16)vol == 0) {
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
        tn = _svm_voice[i].fake_program * 16 + _svm_voice[i].tone;
        v = _svm_voice[i].voll1 * (u16)vol / 127;
        v = v * (_svm_vh->mvol * 0x3FFF) / 0x3F01;
        v = v * _svm_pg[prog].mvol * _svm_tn[tn].vol / 0x3F01;
        voll = v * score->voll / 127;
        volr = v * score->volr / 127;
        p = _svm_tn[tn].pan;
        if (p < 64) {
            volr = volr * p / 63;
        } else {
            voll = voll * (127 - p) / 63;
        }
        p = _svm_pg[prog].mpan;
        if (p < 64) {
            volr = volr * p / 63;
        } else {
            voll = voll * (127 - p) / 63;
        }
        p = (u8)pan;
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
        _svm_voice[i].pan = pan;
        ((short*)_svm_sreg_buf)[i * 8 + 0] = voll * voll / 0x3FFF;
        ((short*)_svm_sreg_buf)[i * 8 + 1] = volr * volr / 0x3FFF;
        _svm_sreg_dirty[i] |= 3;
        count++;
    }
    return count;
}
#endif
