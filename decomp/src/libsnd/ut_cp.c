#include "libsnd_private.h"

#ifdef __psyz
/* The public entry points below hold the interrupt lock (sound API vs. the
 * sequencer tick, see libsnd_private.h). */
void Psyz_KernelLockInterrupts(void);
void Psyz_KernelUnlockInterrupts(void);
#define SsUtChangePitch SsUtChangePitch_Unlocked
#endif

#ifndef __psyz
short SsUtChangePitch(short voice, short vabId, short prog, short old_note,
                      short old_fine, short new_note, short new_fine) {
    unsigned short pitch;

    (void)old_fine;
    if (voice >= 0 && voice < NUM_VOICES) {
        if (_svm_voice[voice].vabId == vabId &&
            _svm_voice[voice].prog == prog &&
            _svm_voice[voice].note == old_note) {
            if (_SsVmVSetUp(vabId, prog)) {
                return -1;
            }
            _svm_cur.tone = _svm_voice[voice].tone;
            pitch = note2pitch2(new_note, new_fine);
            _svm_sreg_buf[voice].pitch = pitch;
            _svm_sreg_dirty[voice] |= 4;
            _svm_voice[voice].unk04 = pitch;
            return 0;
        }
    }
    return -1;
}
#else
short SsUtChangePitch(short voice, short vabId, short prog, short old_note,
                      short old_fine, short new_note, short new_fine) {
    unsigned short pitch;

    (void)old_fine;
    if ((u16)voice >= NUM_VOICES || _svm_voice[voice].vabId != vabId ||
        _svm_voice[voice].prog != prog || _svm_voice[voice].note != old_note) {
        return -1;
    }
    _SsVmVSetUp(vabId, prog);
    _svm_cur.seq_sep_no = 0x21;
    _svm_cur.voice = voice;
    _svm_cur.tone = _svm_voice[voice].tone;
    pitch = note2pitch2(new_note, new_fine);
    _svm_sreg_buf[voice].pitch = pitch;
    _svm_sreg_dirty[voice] |= 4;
    return 0;
}
#endif

#ifdef __psyz
#undef SsUtChangePitch
short SsUtChangePitch(short voice, short vabId, short prog, short old_note,
                      short old_fine, short new_note, short new_fine) {
    short ret;

    Psyz_KernelLockInterrupts();
    ret = SsUtChangePitch_Unlocked(
        voice, vabId, prog, old_note, old_fine, new_note, new_fine);
    Psyz_KernelUnlockInterrupts();
    return ret;
}
#endif
