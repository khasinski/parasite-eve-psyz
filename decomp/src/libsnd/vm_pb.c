#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_pb", _SsVmPBVoice);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_pb", _SsVmPitchBend);
#else
/* Bends a sounding voice of the program: bend 0x40 is the centre, the
 * tone's pbmax/pbmin give the range in semitones up/down. */
short _SsVmPBVoice(
    short voice, short seq_sep_no, short vabId, short prog, short bend) {
    short delta = bend - 0x40;
    unsigned short note, tone;
    int amount, fine;

    if (_svm_voice[voice].seq_sep_no != seq_sep_no ||
        _svm_voice[voice].vabId != vabId || _svm_voice[voice].prog != prog) {
        return 0;
    }
    tone = _svm_voice[voice].tone + _svm_cur.fake_program * 16;
    note = _svm_voice[voice].note;
    if (delta > 0) {
        amount = delta * _svm_tn[tone].pbmax;
        note += amount / 63;
        fine = (amount - amount / 63 * 63) * 2;
    } else if (delta < 0) {
        amount = delta * _svm_tn[tone].pbmin;
        note = note + amount / 64 - 1;
        fine = (amount - amount / 64 * 64) * 2 + 0x7F;
    } else {
        fine = 0;
    }
    _svm_cur.tone = _svm_voice[voice].tone;
    _svm_cur.voice = voice;
    _svm_sreg_buf[voice].pitch = note2pitch2(note, fine & 0xFFFF);
    _svm_sreg_dirty[voice] |= 4;
    return 1;
}

int _SsVmPitchBend(short seq_sep_no, short vabId, short prog, short bend) {
    short i;
    int count = 0;

    _SsVmVSetUp(vabId, prog);
    _svm_cur.seq_sep_no = seq_sep_no;
    for (i = 0; i < _SsVmMaxVoice; i++) {
        count += _SsVmPBVoice(i, seq_sep_no, vabId, prog, (u16)bend);
    }
    return count;
}
#endif
