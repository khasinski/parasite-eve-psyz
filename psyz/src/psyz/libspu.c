#include <psyz.h>
#include <libspu.h>
#include <psyz/log.h>
#include "../../decomp/src/libspu/libspu_private.h"

static SPU_RXX spu_RXX;
static unsigned _dma_dpcr;
union SpuUnion* _spu_RXX = (union SpuUnion*)&spu_RXX;
unsigned* dma_dpcr = &_dma_dpcr; // TODO this must be removed
volatile u16 _spu_RQ[10] = {0};

/* Pitch register to note << 8 | fine, relative to the voice's sample note
 * (PsyQ _spu_pitch2note). Returns -1 outside the MIDI range. */
static int pitch_to_note(int base_note, int base_fine, unsigned pitch) {
    int highest = 0;
    for (int bit = 0; bit < 16; bit++) {
        if ((pitch >> bit) & 1) {
            highest = bit;
        }
    }
    int octave_base = 1 << highest;
    int range = (1 << (highest + 1)) - octave_base;
    int remaining = (int)pitch - octave_base;
    int semitones = remaining * 12 / range;
    int note = base_note + (highest - 12) * 12 + semitones;
    int fine =
        base_fine + (remaining - range * semitones / 12) * 0x600 / range;
    note += fine >= 0x80;
    if (note < 0 || note >= 128) {
        return -1;
    }
    return (note << 8) | (fine % 128);
}

static int volume_mode(u16* value) {
    static const int modes[8] = {1, 2, 3, 4, 5, 6, 7, 7};
    if (!(*value & 0x8000)) {
        return 0;
    }
    int mode = modes[(*value >> 12) & 7];
    *value &= 0xFFF;
    return mode;
}

static short volume_value(u16 value) {
    return (short)(value < 0x4000 ? value : value - 0x8000);
}

/* Read back a voice's attributes from the SPU registers (PsyQ
 * SpuNGetVoiceAttr; the reconstruction in Crash Bandicoot's libspu). */
void SpuNGetVoiceAttr(int vNum, SpuVoiceAttr* arg) {
    if (!arg || vNum < 0 || vNum >= NUM_VOICES) {
        return;
    }
    u16 left = SPURV(vNum, volume.left);
    u16 right = SPURV(vNum, volume.right);
    arg->volmode.left = (short)volume_mode(&left);
    arg->volmode.right = (short)volume_mode(&right);
    arg->volume.left = volume_value(left);
    arg->volume.right = volume_value(right);
    arg->volumex.left = (short)Psyz_SpuRead(0x200 + vNum * 4);
    arg->volumex.right = (short)Psyz_SpuRead(0x202 + vNum * 4);
    arg->pitch = SPURV(vNum, pitch);
    u16 center = _spu_voice_centerNote[vNum];
    int note = pitch_to_note(center >> 8, center & 0xFF, arg->pitch);
    arg->note = (u_short)(note >= 0 ? note : 0);
    arg->sample_note = center;
    arg->envx = (short)SPURV(vNum, volumex);
    arg->addr = (u_long)SPURV(vNum, addr) << _spu_mem_mode_plus;
    arg->loop_addr = (u_long)SPURV(vNum, loop_addr) << _spu_mem_mode_plus;
    u16 adsr1 = SPURV(vNum, adsr[0]);
    u16 adsr2 = SPURV(vNum, adsr[1]);
    arg->a_mode = (adsr1 & 0x80) ? 5 : 1;
    /* As in the matched reconstruction: any of bits 8-9 reads back as 7. */
    arg->s_mode = (adsr2 & 0x300) ? 7 : 1;
    arg->r_mode = (adsr2 & 0x20) ? 7 : 3;
    arg->ar = adsr1 >> 8;
    arg->dr = (adsr1 & 0xF0) >> 4;
    arg->sr = (adsr2 >> 6) & 0xFCFF;
    arg->rr = adsr2 & 0x1F;
    arg->sl = adsr1 & 0xF;
    arg->adsr1 = adsr1;
    arg->adsr2 = adsr2;
}

void SpuGetVoiceEnvelope(int voiceNum, short* envx) {
    if (!envx) return;
    *envx = voiceNum >= 0 && voiceNum < NUM_VOICES
                ? (short)SPURV(voiceNum, volumex) : 0;
}

void SpuSetReverbModeDepth(short depthL, short depthR) {
    SPUW(rev_vol.left, (u16)depthL);
    SPUW(rev_vol.right, (u16)depthR);
}

int SpuSetReverbModeType(unsigned int mode) {
    SpuReverbAttr attr = {0};
    attr.mask = SPU_REV_MODE;
    attr.mode = (int)mode;
    return (int)SpuSetReverbModeParam(&attr);
}
