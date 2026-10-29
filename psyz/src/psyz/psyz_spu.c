#include <psyz.h>
#include <psyz/log.h>
#include <assert.h>
#include <string.h>
#include "spu_voice.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(SPU_RXX) == 0x200, "SPU_RXX must be 0x200 bytes");
_Static_assert(
    sizeof(union SpuUnion) == sizeof(SPU_RXX), "SpuUnion must alias SPU_RXX");
#endif

#define N_CHANNELS 2                      // stereo interleaved
#define CD_RING_FRAMES 4096               // must be power of 2
#define CD_RING_MASK (CD_RING_FRAMES - 1) // ring wrapper
#define CD_RING_LOW_WATER 1024            // refill when short of N frames

// Full SPU state
static struct {
    u8 ram[PSYZ_SPU_RAM_SIZE];

    // Transfer address (byte address into SPU RAM)
    unsigned transfer_addr;

    // Capture buffer position
    u16 capture_pos;

    unsigned reverb_cur;
    unsigned reverb_phase;
    int noise_timer;
    u16 noise_level;
    short reverb_input[2][64];
    short reverb_output[2][64];

    VoiceState voice[PSYZ_SPU_NUM_VOICES];
    short main_volume[2];

    // CD audio ring buffer, refilled by Psyz_CdPullSamples
    short cd_ring[CD_RING_FRAMES * N_CHANNELS];
    unsigned cd_ring_read;
    unsigned cd_ring_count; // number of valid frames in ring

    u8 initialized;
} spu;

u8* Psyz_SpuGetRam(void) { return spu.ram; }

void Psyz_SpuInit(void) {
    Psyz_AudioLock();
    if (!spu.initialized) {
        memset(&spu, 0, sizeof(spu));
        spu.initialized = 1;
        INFOF("SPU emulation initialized");
    }
    Psyz_AudioUnlock();
}

static void spu_reset(void) {
    _spu_RXX->rxx.spustat = 0;
    for (int v = 0; v < PSYZ_SPU_NUM_VOICES; v++) {
        _spu_RXX->rxx.voice[v].volumex = 0;
    }
    memset(&spu, 0, sizeof(spu));
}

static void spu_reset_hot(void) {
    u8 saved_ram[PSYZ_SPU_RAM_SIZE];
    memcpy(saved_ram, spu.ram, PSYZ_SPU_RAM_SIZE);
    spu_reset();
    memcpy(spu.ram, saved_ram, PSYZ_SPU_RAM_SIZE);
}

void Psyz_SpuReset(int hot) {
    Psyz_AudioLock();
    if (hot) {
        spu_reset_hot();
    } else {
        spu_reset();
    }
    spu.initialized = 1;
    Psyz_AudioUnlock();
}

void Psyz_SpuSetTransferAddr(unsigned int addr) {
    Psyz_AudioLock();
    spu.transfer_addr = addr & (PSYZ_SPU_RAM_SIZE - 1);
    Psyz_AudioUnlock();
}

unsigned int Psyz_SpuGetTransferAddr(void) {
    Psyz_AudioLock();
    unsigned int address = spu.transfer_addr;
    Psyz_AudioUnlock();
    return address;
}

void Psyz_SpuFifoWrite(unsigned short word) {
    Psyz_AudioLock();
#ifdef PLATFORM_LE
    *(unsigned short*)&spu.ram[spu.transfer_addr] = word;
#else
    spu.ram[spu.transfer_addr] = (unsigned char)(word & 0xFF);
    spu.ram[spu.transfer_addr + 1] = (unsigned char)((word >> 8) & 0xFF);
#endif
    spu.transfer_addr = (spu.transfer_addr + 2) & (PSYZ_SPU_RAM_SIZE - 1);
    Psyz_AudioUnlock();
}

void Psyz_SpuFifoWriteBulk(const unsigned char* src, unsigned int size) {
    Psyz_AudioLock();
    Psyz_SpuMemWrite(spu.transfer_addr, src, size);
    spu.transfer_addr = (spu.transfer_addr + size) & (PSYZ_SPU_RAM_SIZE - 1);
    Psyz_AudioUnlock();
}

void Psyz_SpuMemRead(unsigned int offset, void* dst, unsigned int size) {
    Psyz_AudioLock();
    unsigned int start = offset & (PSYZ_SPU_RAM_SIZE - 1);
    unsigned int head = PSYZ_SPU_RAM_SIZE - start;
    if (size <= head) {
        memcpy(dst, &spu.ram[start], size);
    } else {
        memcpy(dst, &spu.ram[start], head);
        memcpy((unsigned char*)dst + head, &spu.ram[0], size - head);
    }
    Psyz_AudioUnlock();
}

void Psyz_SpuMemWrite(unsigned int offset, const void* src, unsigned int size) {
    Psyz_AudioLock();
    unsigned int start = offset & (PSYZ_SPU_RAM_SIZE - 1);
    unsigned int head = PSYZ_SPU_RAM_SIZE - start;
    if (size <= head) {
        memcpy(&spu.ram[start], src, size);
    } else {
        memcpy(&spu.ram[start], src, head);
        memcpy(&spu.ram[0], (const unsigned char*)src + head, size - head);
    }
    Psyz_AudioUnlock();
}

static void spu_key_on_voice(int v) {
    spu_voice_key_on(&spu.voice[v], &_spu_RXX->rxx.voice[v]);
    spu.voice[v].delay_ticks = ENV_KEYON_DELAY_TICKS - 1;
}

void Psyz_SpuWrite(unsigned int reg_offset, unsigned short value) {
    if (reg_offset >= sizeof(SPU_RXX) || (reg_offset & 1)) {
        WARNF("Psyz_SpuWrite: bad offset 0x%X", reg_offset);
        return;
    }
    if (reg_offset == offsetof(SPU_RXX, spustat)) { // read-only
        return;
    }
    // Register strobes latch at a sample boundary. Serialize their pending
    // flags with mixing; a retrigger must retain the current output sample.
    Psyz_AudioLock();
    _spu_RXX->raw[reg_offset >> 1] = value;
    // MSVC's C-mode offsetof is not an integer constant expression, so it
    // cannot appear in a case label. Use an if-chain instead.
    if (reg_offset == offsetof(SPU_RXX, key_on[0])) { // voices 0..15
        for (int v = 0; v < 16; v++) {
            if (value & (1u << v))
                spu.voice[v].key_on_pending = 1;
        }
    } else if (reg_offset == offsetof(SPU_RXX, key_on[1])) { // voices 16..23
        for (int v = 0; v < 8; v++) {
            if (value & (1u << v))
                spu.voice[16 + v].key_on_pending = 1;
        }
    } else if (reg_offset == offsetof(SPU_RXX, key_off[0])) { // voices 0..15
        for (int v = 0; v < 16; v++) {
            if (value & (1u << v))
                spu.voice[v].key_off = 1;
        }
    } else if (reg_offset == offsetof(SPU_RXX, key_off[1])) { // voices 16..23
        for (int v = 0; v < 8; v++) {
            if (value & (1u << v))
                spu.voice[16 + v].key_off = 1;
        }
    } else if (reg_offset == offsetof(SPU_RXX, trans_addr)) {
        Psyz_SpuSetTransferAddr((unsigned)value << 3);
    } else if (reg_offset == offsetof(SPU_RXX, rev_work_addr)) {
        spu.reverb_cur = ((unsigned)value << 3) & 0x7FFFF;
    } else if (reg_offset == offsetof(SPU_RXX, trans_fifo)) {
        Psyz_SpuFifoWrite(value);
    }
    Psyz_AudioUnlock();
}

unsigned short Psyz_SpuRead(unsigned int reg_offset) {
    if (reg_offset >= sizeof(SPU_RXX) || (reg_offset & 1)) {
        WARNF("Psyz_SpuRead: bad offset 0x%X", reg_offset);
        return 0;
    }
    Psyz_AudioLock();
    unsigned short value;
    // offsetof is not an ICE under MSVC C mode, so it cannot be a case label.
    if (reg_offset == offsetof(SPU_RXX, trans_addr)) {
        value = (unsigned short)((spu.transfer_addr >> 3) & 0xFFFF);
    } else if (reg_offset == offsetof(SPU_RXX, spustat)) {
        // lower 6 bits mirror SPUCNT's low bits
        // bit 11, capture-buffer half-pointer, flipped by spu_tick.
        value = (_spu_RXX->rxx.spucnt & 0x3F) |
                (_spu_RXX->rxx.spustat & (1u << 11));
    } else {
        value = _spu_RXX->raw[reg_offset >> 1];
    }
    Psyz_AudioUnlock();
    return value;
}

static void write_capture(unsigned int idx, short val) {
    unsigned int addr = (idx * 0x400) | spu.capture_pos;
    if (addr < PSYZ_SPU_RAM_SIZE) {
        *(short*)&spu.ram[addr] = val;
    }
}

static inline int voice_vol(unsigned short reg) {
    if (reg & 0x8000) {
        // https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm
        LOG_ONCE("voice volume bit15 not implemented");
        return 0;
    }
    return (short)((reg & 0x7FFF) << 1);
}

static inline int spu_s16(u16 value) { return (short)value; }

static unsigned reverb_address(u16 raw_offset, int extra_bytes) {
    unsigned base = ((unsigned)_spu_RXX->rxx.rev_work_addr << 3) & 0x7FFFF;
    unsigned addr = spu.reverb_cur + ((unsigned)raw_offset << 3) + extra_bytes;
    addr &= 0x7FFFF;
    if (addr < base)
        addr = base + addr;
    return addr & 0x7FFFF;
}

static short reverb_read(u16 offset, int extra_bytes) {
    unsigned addr = reverb_address(offset, extra_bytes);
    return (short)(spu.ram[addr] | (spu.ram[(addr + 1) & 0x7FFFF] << 8));
}

static void reverb_write(u16 offset, int value) {
    unsigned addr;
    short sample;
    if (!(_spu_RXX->rxx.spucnt & SPU_CTRL_MASK_REVERB_MASTER_ENABLE))
        return;
    addr = reverb_address(offset, 0);
    sample = clamp16(value);
    spu.ram[addr] = (u8)sample;
    spu.ram[(addr + 1) & 0x7FFFF] = (u8)((u16)sample >> 8);
}

static int reverb_mul(int sample, u16 coefficient) {
    return (sample * spu_s16(coefficient)) >> 15;
}

// Reverb retains one fractional bit across the paired products, then
// rounds their sum. Truncating each product at 15 bits biases the tail.
static int reverb_product14(int sample, int coefficient) {
    return (int)(((int64_t)sample * coefficient) >> 14);
}

static int reverb_inverse_phase(u16 coefficient) {
    int value = spu_s16(coefficient);
    return value == -32768 ? 32767 : -value;
}

static int reverb_iir_previous(int sample, int alpha) {
    if (alpha == -32768)
        return sample == -32768 ? 0 : reverb_product14(sample, -65536);
    return reverb_product14(sample, 32768 - alpha);
}

static short reverb_process_channel(unsigned channel, int sample) {
    SPU_RXX* r = (SPU_RXX*)&_spu_RXX->rxx;
    const u16 same_src[2] = {r->dLSAME, r->dRSAME};
    const u16 diff_src[2] = {r->dRDIFF, r->dLDIFF};
    const u16 same_dst[2] = {r->mLSAME, r->mRSAME};
    const u16 diff_dst[2] = {r->mLDIFF, r->mRDIFF};
    const u16 comb1[2] = {r->mLCOMB1, r->mRCOMB1};
    const u16 comb2[2] = {r->mLCOMB2, r->mRCOMB2};
    const u16 comb3[2] = {r->mLCOMB3, r->mRCOMB3};
    const u16 comb4[2] = {r->mLCOMB4, r->mRCOMB4};
    const u16 apf1[2] = {r->mLAPF1, r->mRAPF1};
    const u16 apf2[2] = {r->mLAPF2, r->mRAPF2};
    const u16 in_coef[2] = {r->vLIN, r->vRIN};
    {
        int iir_input_a = clamp16(
            (reverb_product14(
                 reverb_read(same_src[channel], 0), spu_s16(r->vWALL)) +
             reverb_product14(sample, spu_s16(in_coef[channel]))) >>
            1);
        int iir_input_b = clamp16(
            (reverb_product14(
                 reverb_read(diff_src[channel], 0), spu_s16(r->vWALL)) +
             reverb_product14(sample, spu_s16(in_coef[channel]))) >>
            1);
        int previous_a = reverb_read(same_dst[channel], -2);
        int previous_b = reverb_read(diff_dst[channel], -2);
        int alpha = spu_s16(r->vIIR);
        int iir_a = clamp16((reverb_product14(iir_input_a, alpha) +
                             reverb_iir_previous(previous_a, alpha)) >>
                            1);
        int iir_b = clamp16((reverb_product14(iir_input_b, alpha) +
                             reverb_iir_previous(previous_b, alpha)) >>
                            1);
        int accumulator;
        int feedback_a;
        int feedback_b;
        int mix_a;
        int mix_b;

        reverb_write(same_dst[channel], iir_a);
        reverb_write(diff_dst[channel], iir_b);
        accumulator =
            reverb_product14(
                reverb_read(comb1[channel], 0), spu_s16(r->vCOMB1)) +
            reverb_product14(
                reverb_read(comb2[channel], 0), spu_s16(r->vCOMB2)) +
            reverb_product14(
                reverb_read(comb3[channel], 0), spu_s16(r->vCOMB3)) +
            reverb_product14(
                reverb_read(comb4[channel], 0), spu_s16(r->vCOMB4));
        feedback_a = reverb_read((u16)(apf1[channel] - r->dAPF1), 0);
        feedback_b = reverb_read((u16)(apf2[channel] - r->dAPF2), 0);
        mix_a = clamp16(
            (accumulator +
             reverb_product14(feedback_a, reverb_inverse_phase(r->vAPF1))) >>
            1);
        mix_b = clamp16(
            feedback_a +
            ((reverb_product14(mix_a, spu_s16(r->vAPF1)) +
              reverb_product14(feedback_b, reverb_inverse_phase(r->vAPF2))) >>
             1));
        sample = clamp16(feedback_b + reverb_mul(mix_b, r->vAPF2));
        reverb_write(apf1[channel], mix_a);
        reverb_write(apf2[channel], mix_b);
    }

    // One channel is calculated per 44.1 kHz tick. Both share a work
    // cursor, which advances after the right channel calculation.
    if (channel == 1) {
        spu.reverb_cur = (spu.reverb_cur + 2) & 0x7FFFF;
        if (spu.reverb_cur < ((unsigned)r->rev_work_addr << 3))
            spu.reverb_cur = (unsigned)r->rev_work_addr << 3;
    }
    return (short)sample;
}

// Hardware half-band filter, documented in the SPU reverb specification.
// Input is decimated by two; output inserts zeroes and doubles filter gain.
static const short reverb_fir[39] = {
    -1,   0, 2,     0, -10,   0,     35,    0, -103,  0, 266,  0, -616, 0,
    1332, 0, -2960, 0, 10246, 16384, 10246, 0, -2960, 0, 1332, 0, -616, 0,
    266,  0, -103,  0, 35,    0,     -10,   0, 2,     0, -1};

static int reverb_filter(const short history[64], unsigned cursor, int shift) {
    int sum = 0;
    for (unsigned tap = 0; tap < 39; ++tap)
        sum += reverb_fir[tap] * history[(cursor - tap) & 63];
    return clamp16(sum >> shift);
}

static void reverb_tick(int input_left, int input_right, int output[2]) {
    unsigned cursor = spu.reverb_phase;
    spu.reverb_input[0][cursor] = clamp16(input_left);
    spu.reverb_input[1][cursor] = clamp16(input_right);
    spu.reverb_output[0][cursor] = 0;
    spu.reverb_output[1][cursor] = 0;
    unsigned channel = (cursor & 1) ? 0 : 1;
    int downsampled = reverb_filter(spu.reverb_input[channel], cursor, 15);
    spu.reverb_output[channel][cursor] =
        reverb_process_channel(channel, downsampled);
    output[0] = reverb_filter(spu.reverb_output[0], cursor, 14);
    output[1] = reverb_filter(spu.reverb_output[1], cursor, 14);
    spu.reverb_phase = (cursor + 1) & 63;
}

// generate one frame at 44100hz with voices mix, cd playback and volume control
static void spu_tick(short* out) {
    SPU_RXX* rxx = (SPU_RXX*)&_spu_RXX->rxx;
    unsigned short spucnt = rxx->spucnt;

    // accumulate frame to each separate channels
    int left_sum = 0, right_sum = 0;
    int reverb_input_left = 0, reverb_input_right = 0;
    int reverb_output[2];

    // Pre-fill CD ring buffer if running low
    if (spu.cd_ring_count < CD_RING_LOW_WATER) {
        size_t space = CD_RING_FRAMES - spu.cd_ring_count;
        size_t write_pos =
            (spu.cd_ring_read + spu.cd_ring_count) & CD_RING_MASK;
        // Fill into contiguous chunk up to end of array
        size_t chunk = CD_RING_FRAMES - write_pos;
        if (chunk > space)
            chunk = space;
        size_t got = Psyz_CdPullSamples(&spu.cd_ring[write_pos * 2], chunk);
        spu.cd_ring_count += got;
    }

    // Read one stereo frame from CD ring buffer
    int cd_left = 0, cd_right = 0;
    if (spu.cd_ring_count > 0) {
        cd_left = spu.cd_ring[spu.cd_ring_read * 2];
        cd_right = spu.cd_ring[spu.cd_ring_read * 2 + 1];
        spu.cd_ring_read = (spu.cd_ring_read + 1) & CD_RING_MASK;
        spu.cd_ring_count--;
    }

    // One shared noise generator runs at the SPUCNT clock, independently
    // of voice pitch. The feedback includes the inverted bit in the SPU LFSR.
    spu.noise_timer -= 4 + ((spucnt >> 8) & 3);
    if (spu.noise_timer < 0) {
        unsigned level = spu.noise_level;
        unsigned feedback = ((level >> 15) ^ (level >> 12) ^ (level >> 11) ^
                             (level >> 10) ^ 1) &
                            1;
        spu.noise_level = (u16)((level << 1) | feedback);
        int period = 0x20000 >> ((spucnt >> 10) & 15);
        spu.noise_timer += period;
        if (spu.noise_timer < 0)
            spu.noise_timer += period;
    }

    // decode+resample, scale volume by ADSR envelope, then mix voices
    int main_left = spu.main_volume[0], main_right = spu.main_volume[1];
    spu.main_volume[0] = (short)voice_vol(rxx->main_vol.left);
    spu.main_volume[1] = (short)voice_vol(rxx->main_vol.right);
    short v1_sample = 0, v3_sample = 0, previous_sample = 0;
    for (int v = 0; v < PSYZ_SPU_NUM_VOICES; v++) {
        // Volume controls latch after the current sample, independently of
        // key-on or envelope activity. Retain their old gain for this mix.
        int left_gain = spu.voice[v].volume_current[0];
        int right_gain = spu.voice[v].volume_current[1];
        spu.voice[v].volume_current[0] =
            (short)voice_vol(rxx->voice[v].volume.left);
        spu.voice[v].volume_current[1] =
            (short)voice_vol(rxx->voice[v].volume.right);
        if (!spu.voice[v].active) {
            previous_sample = 0;
            if (spu.voice[v].key_on_pending)
                spu_key_on_voice(v);
            continue;
        }
        // AKAO changes attack/release on voices that are already playing.
        // ADSR registers remain live after key-on, like pitch and volume.
        spu.voice[v].adsr_lo = rxx->voice[v].adsr[0];
        spu.voice[v].adsr_hi = rxx->voice[v].adsr[1];
        unsigned pitch = rxx->voice[v].pitch;
        unsigned word = (unsigned)v >> 4;
        unsigned bit = 1u << (v & 15);
        if (v && (rxx->chan_fm[word] & bit)) {
            // FM uses the preceding voice after ADSR, before stereo volume.
            int modulated = ((short)pitch * (previous_sample + 32768)) >> 15;
            pitch = (u16)modulated;
        }
        if (pitch > 0x3FFF)
            pitch = 0x3FFF;
        if (spu.voice[v].delay_ticks) {
            spu.voice[v].delay_ticks--;
            rxx->voice[v].volumex = 0;
            previous_sample = 0;
            if (spu.voice[v].key_on_pending)
                spu_key_on_voice(v);
            continue;
        }
        spu.voice[v].noise_mode = (rxx->noise_mode[word] & bit) != 0;
        short s = spu_voice_step(&spu.voice[v], pitch, spu.ram);
        if (rxx->noise_mode[word] & bit)
            s = (short)spu.noise_level;
        if (spu.voice[v].env_state == ADSR_OFF) {
            s = 0;
        }
        s = (short)(((int)s * spu.voice[v].env_vol) >> 15);
        // ENVX advances after this sample has been scaled. It is readable
        // immediately, and supplies the gain of the next output sample.
        spu_voice_envelope_step(&spu.voice[v]);
        rxx->voice[v].volumex = (unsigned short)spu.voice[v].env_vol;
        previous_sample = s;
        if (v == 1) {
            v1_sample = s;
        } else if (v == 3) {
            v3_sample = s;
        }
        {
            int voice_left = (s * left_gain) >> 15;
            int voice_right = (s * right_gain) >> 15;
            left_sum += voice_left;
            right_sum += voice_right;
            if ((v < 16 && (rxx->rev_mode[0] & (1u << v))) ||
                (v >= 16 && (rxx->rev_mode[1] & (1u << (v - 16))))) {
                reverb_input_left += voice_left;
                reverb_input_right += voice_right;
            }
        }
        // KON resets the decoder/envelope after this sample has fed the
        // mixer and the next voice's FM input, as on the SPU pipeline.
        if (spu.voice[v].key_on_pending)
            spu_key_on_voice(v);
    }

    // Mute all voices. CD audio is mixed after, and not affected by mute.
    if (!(spucnt & SPU_CTRL_MASK_MUTE_SPU)) {
        left_sum = right_sum = 0;
        reverb_input_left = reverb_input_right = 0;
    }

    reverb_tick(
        clamp16(reverb_input_left), clamp16(reverb_input_right), reverb_output);
    left_sum += reverb_mul(reverb_output[0], rxx->rev_vol.left);
    right_sum += reverb_mul(reverb_output[1], rxx->rev_vol.right);

    // Mix CD audio per SPUCNT and cd_vol registers.
    if (spucnt & SPU_CTRL_MASK_CD_AUDIO_ENABLE) {
        left_sum += (cd_left * rxx->cd_vol.left) >> 15;
        right_sum += (cd_right * rxx->cd_vol.right) >> 15;
    }

    // The SPU saturates the mixed bus before applying signed master volume.
    // This also bounds the product when many loud voices overlap.
    out[0] = (short)((clamp16(left_sum) * main_left) >> 15);
    out[1] = (short)((clamp16(right_sum) * main_right) >> 15);

    // Capture buffers back to SPU RAM, as per real hardware
    write_capture(0, (short)cd_left);
    write_capture(1, (short)cd_right);
    write_capture(2, v1_sample);
    write_capture(3, v3_sample);

    // SPUSTAT bit 11 flips when capture_pos crosses 0x200
    unsigned prev_pos = spu.capture_pos;
    spu.capture_pos = (prev_pos + 2) & 0x3FF;
    if ((prev_pos ^ spu.capture_pos) & 0x200) {
        rxx->spustat ^= 1u << 11;
    }
}

void Psyz_SpuFlushCdSamples(void) {
    Psyz_AudioLock();
    spu.cd_ring_read = spu.cd_ring_count = 0;
    Psyz_AudioUnlock();
}

void Psyz_SpuPullSamples(short* out, int num_frames) {
    Psyz_AudioLock();
    if (!spu.initialized) {
        memset(out, 0, num_frames * 2 * sizeof(short));
    } else {
        for (int i = 0; i < num_frames; i++) {
            spu_tick(&out[i * 2]);
        }
    }
    Psyz_AudioUnlock();
}
