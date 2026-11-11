#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/ccadsr", _SsUtResolveADSR);

INCLUDE_ASM("asm/nonmatchings/libsnd/ccadsr", _SsUtBuildADSR);
#else
void _SsUtResolveADSR(u16 adsr1, u16 adsr2, struct Unk* adsr) {
    adsr->attack_exp = adsr1 & 0x8000;
    adsr->sustain_exp = adsr2 & 0x8000;
    adsr->sustain_decrease = adsr2 & 0x4000;
    adsr->release_exp = adsr2 & 0x20;
    adsr->attack_rate = (adsr1 >> 8) & 0x7F;
    adsr->decay_rate = (adsr1 >> 4) & 0xF;
    adsr->sustain_level = adsr1 & 0xF;
    adsr->sustain_rate = (adsr2 >> 6) & 0x7F;
    adsr->release_rate = adsr2 & 0x1F;
}

void _SsUtBuildADSR(
    struct Unk* adsr, unsigned short* adsr1, unsigned short* adsr2) {
    unsigned short hi = adsr->sustain_exp ? 0x8000 : 0;

    if (adsr->sustain_decrease) {
        hi |= 0x4000;
    }
    if (adsr->release_exp) {
        hi |= 0x20;
    }
    *adsr1 = (adsr->attack_exp ? 0x8000 : 0) |
             ((adsr->attack_rate << 8) & 0x7F00) |
             ((adsr->decay_rate << 4) & 0xF0) | (adsr->sustain_level & 0xF);
    *adsr2 =
        hi | ((adsr->sustain_rate << 6) & 0x1FC0) | (adsr->release_rate & 0x1F);
}
#endif
