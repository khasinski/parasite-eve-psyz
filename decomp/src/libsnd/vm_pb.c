#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_pb", _SsVmPBVoice);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_pb", _SsVmPitchBend);

#else
/* Bend one voice. pitch is the 7-bit MSB, 64 = center; the bend range is the
 * tone's pbmin/pbmax in semitones. */
short _SsVmPBVoice(short voice, short seq_sep_no, short vabId, short prog,
                   short pitch) {
    int bend = pitch - 64;
    int tn;
    int note;
    int fine;
    int prod;

    if (_svm_voice[voice].seq_sep_no != seq_sep_no ||
        _svm_voice[voice].vabId != vabId || _svm_voice[voice].prog != prog) {
        return 0;
    }
    tn = _svm_cur.fake_program * 16 + _svm_voice[voice].tone;
    note = _svm_voice[voice].note;
    if (bend > 0) {
        prod = bend * _svm_tn[tn].pbmax;
        note += prod / 63;
        fine = (prod % 63) * 2;
    } else if (bend < 0) {
        prod = bend * _svm_tn[tn].pbmin;
        note += prod / 64 - 1;
        fine = (prod % 64) * 2 + 127;
    } else {
        fine = 0;
    }
    _svm_cur.tone = _svm_voice[voice].tone;
    _svm_cur.voice = voice;
    ((short*)_svm_sreg_buf)[voice * 8 + 2] = note2pitch2(note, fine);
    _svm_sreg_dirty[voice] |= 4;
    return 1;
}

short _SsVmPitchBend(short seq_sep_no, short vabId, short prog, short pitch) {
    short i;
    short count = 0;

    if (_SsVmVSetUp(vabId, prog) != 0) {
        return 0;
    }
    _svm_cur.seq_sep_no = seq_sep_no;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        count += _SsVmPBVoice(i, seq_sep_no, vabId, prog, pitch);
    }
    return count;
}
#endif
