#include "libsnd_private.h"

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
