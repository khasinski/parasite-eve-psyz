#include "libsnd_private.h"

#ifndef __psyz
void vmNoiseOff(char voice) {
    _svm_voice[voice].unk1b = 0;
    _svm_voice[voice].unk04 = 0;
    SPUW(noise_mode[0], 0);
    SPUW(noise_mode[1], 0);
}
#else
/* PSY-Q 4.x only releases the voice; the flush turns the noise off. */
void vmNoiseOff(char voice) {
    _svm_voice[(u8)voice].unk1b = 0;
    _svm_voice[(u8)voice].vag_idx = 0;
    _svm_voice[(u8)voice].unk04 = 0;
}
#endif
