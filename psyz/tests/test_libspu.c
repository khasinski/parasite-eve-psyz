#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psyz.h>
#include <libspu.h>

#include "ztest.h"

#ifdef __psyz
#include "../src/psyz/spu_voice.h"
#endif

static void spu_setup(void) {
    Psyz_SpuInit();
    Psyz_SpuReset(0);
}

ZTEST_SETUP(spu) {
    zskip_targets("ps1"); // TODO tests must be validated on real hardware!!
    Psyz_AudioPause();
    Psyz_AudioLock();
    spu_setup();
}

ZTEST_TEARDOWN(spu) {
    Psyz_AudioUnlock();
    Psyz_AudioUnpause();
}

#ifdef __psyz
ZTEST(spu, GaussianHardwareImpulse) {
    // Independently tabulated PS1 ROM values at five phases. These differ
    // from both the SNES ROM and the previously normalized approximation.
    static const unsigned phases[] = {0, 64, 128, 192, 255};
    static const int taps[5][4] = {
        {4807, 22963, 4871, -1},
        {1756, 20944, 9901, 39},
        {412, 15855, 15948, 424},
        {37, 9811, 21004, 1789},
        {-1, 4871, 22963, 4807}};
    for (unsigned phase = 0; phase < 5; ++phase) {
        for (unsigned tap = 0; tap < 4; ++tap) {
            for (unsigned negative = 0; negative < 2; ++negative) {
                VoiceState voice = {0};
                int amplitude = negative ? -32768 : 32767;
                voice.active = 1;
                voice.spos = phases[phase] << 8;
                voice.gwin[tap] = (short)amplitude;
                zexpect_s32_eq((taps[phase][tap] * amplitude) >> 15,
                               spu_voice_step(&voice, 0, NULL));
            }
        }
        VoiceState voice = {0};
        const short samples[] = {-30123, 17291, -8193, 32511};
        int expected = 0;
        voice.active = 1;
        voice.spos = phases[phase] << 8;
        for (unsigned tap = 0; tap < 4; ++tap) {
            voice.gwin[tap] = samples[tap];
            expected += taps[phase][tap] * samples[tap];
        }
        zexpect_s32_eq(expected >> 15, spu_voice_step(&voice, 0, NULL));
    }
}
#endif

ZTEST(spu, VoiceGainLatchReference) {
    // Synthetic ramp rendered by independent Beetle PSX SPU 9164674, with
    // frontend attenuation removed. Gain writes take effect after this tick.
    static const unsigned char ramp[16] = {
        0,    7,    0x10, 0x32, 0x54, 0x76, 0x77, 0x65,
        0x43, 0x21, 0xef, 0xcd, 0xab, 0x89, 0x88, 0x9a};
    static const short expected[128] = {
        -11464, -11464, -8059,  -4030,  -10377, -5189,  -12696, -6348,  -15006,
        -7503,  -16290, -8145,  -16192, -8096,  -13524, -6762,  -12333, -6167,
        -1402,  -701,   2424,   1211,   4742,   2370,   7061,   3530,   9387,
        4693,   11705,  5852,   13831,  6915,   14275,  7137,   13308,  6653,
        11045,  5522,   9510,   4754,   7171,   3585,   4180,   2089,   3090,
        1544,   -1347,  -674,   -4314,  -2157,  -6641,  -3321,  -8959,  -4480,
        -11278, -5639,  -13596, -6798,  -15779, -7890,  -16317, -8159,  -15535,
        -7768,  -13055, -6528,  -4357,  -1090,  422,    105,    1663,   415,
        2821,   705,    3985,   996,    5144,   1286,   6303,   1575,   7098,
        1774,   7118,   1779,   5983,   1495,   5646,   1411,   3831,   957,
        3464,   866,    1518,   379,    988,    247,    -1404,  -351,   -2612,
        -653,   -3771,  -943,   -4930,  -1233,  -6089,  -1523,  -7252,  -1813,
        -8103,  -2026,  -8148,  -2037,  -7118,  -1780,  -6520,  -1630,  -1848,
        -462,   953,    238,    2112,   528,    3276,   819,    4435,   1108,
        5594,   1398,
    };
    short warmup[1024], pcm[128];
    Psyz_SpuReset(0);
    Psyz_SpuMemWrite(0x1000, ramp, sizeof(ramp));
    Psyz_SpuWrite(0x1AA, 0xC000);
    Psyz_SpuWrite(0x180, 0x3FFF);
    Psyz_SpuWrite(0x182, 0x3FFF);
    Psyz_SpuWrite(0x10, 0x3FFF);
    Psyz_SpuWrite(0x12, 0x3FFF);
    Psyz_SpuWrite(0x14, 0x1234);
    Psyz_SpuWrite(0x16, 0x200);
    Psyz_SpuWrite(0x18, 0x00FF);
    Psyz_SpuWrite(0x1A, 0x1FC0);
    Psyz_SpuWrite(0x1E, 0x200);
    Psyz_SpuWrite(0x188, 2);
    Psyz_SpuPullSamples(warmup, 512);
    Psyz_SpuWrite(0x10, 0x2000);
    Psyz_SpuWrite(0x12, 0x1000);
    Psyz_SpuPullSamples(pcm, 32);
    Psyz_SpuWrite(0x180, 0x2000);
    Psyz_SpuWrite(0x182, 0x1000);
    Psyz_SpuPullSamples(pcm + 64, 32);
    for (unsigned i = 0; i < 128; ++i)
        zexpect_s32_eq(expected[i], pcm[i]);
}

ZTEST(spu, SetTransferAddrMasksToRamRange) {
    Psyz_SpuSetTransferAddr(PSYZ_SPU_RAM_SIZE + 0x10);
    zexpect_u32_eq(0x10, Psyz_SpuGetTransferAddr());
}

ZTEST(spu, WriteXferAddrRegSetsTransferAddr) {
    Psyz_SpuWrite(0x1A6, 0x0200);
    zexpect_u32_eq(0x1000, Psyz_SpuGetTransferAddr());
    zexpect_u16_eq(0x0200, Psyz_SpuRead(0x1A6));

    Psyz_SpuWrite(0x1A6, 0x0201);
    zexpect_u32_eq(0x1008, Psyz_SpuGetTransferAddr());
    zexpect_u16_eq(0x0201, Psyz_SpuRead(0x1A6));
}

ZTEST(spu, SetTransferAddrWritesXferAddrReg) {
    Psyz_SpuSetTransferAddr(0x1000u);
    zexpect_u16_eq(0x0200, Psyz_SpuRead(0x1A6));

    Psyz_SpuSetTransferAddr(0x1008u);
    zexpect_u16_eq(0x0201, Psyz_SpuRead(0x1A6));
}

ZTEST(spu, MemWriteAndReadByteForByte) {
    unsigned char payload[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    unsigned char buf[8] = {0};
    size_t i;
    Psyz_SpuMemWrite(0x3000, payload, sizeof(payload));
    Psyz_SpuMemRead(0x3000, buf, sizeof(buf));
    for (i = 0; i < sizeof(payload); i++) {
        if (!zexpect_u8_eq(payload[i], buf[i])) {
            zprintf("byte %zu\n", i);
        }
    }
}

ZTEST(spu, MemWriteDoesNotMoveFifoCursor) {
    unsigned char payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    Psyz_SpuSetTransferAddr(0x4000);
    Psyz_SpuMemWrite(0x8000, payload, sizeof(payload));
    zexpect_u32_eq(0x4000, Psyz_SpuGetTransferAddr());
    zexpect_u8_eq(0xDE, Psyz_SpuGetRam()[0x8000]);
    zexpect_u8_eq(0xEF, Psyz_SpuGetRam()[0x8003]);
}

ZTEST(spu, MemReadWriteWrapsAtRamRange) {
    unsigned char payload[4] = {0x11, 0x22, 0x33, 0x44};
    unsigned char buf[4] = {0};
    Psyz_SpuMemWrite(PSYZ_SPU_RAM_SIZE - 2, payload, sizeof(payload));
    zexpect_u8_eq(0x11, Psyz_SpuGetRam()[PSYZ_SPU_RAM_SIZE - 2]);
    zexpect_u8_eq(0x22, Psyz_SpuGetRam()[PSYZ_SPU_RAM_SIZE - 1]);
    zexpect_u8_eq(0x33, Psyz_SpuGetRam()[0]);
    zexpect_u8_eq(0x44, Psyz_SpuGetRam()[1]);

    Psyz_SpuMemRead(PSYZ_SPU_RAM_SIZE - 2, buf, sizeof(buf));
    zexpect_u8_eq(0x11, buf[0]);
    zexpect_u8_eq(0x22, buf[1]);
    zexpect_u8_eq(0x33, buf[2]);
    zexpect_u8_eq(0x44, buf[3]);
}

ZTEST(spu, FifoWrite) {
    Psyz_SpuSetTransferAddr(0x1000);
    zexpect_u32_eq(0x1000, Psyz_SpuGetTransferAddr());
    Psyz_SpuFifoWrite(0xDEAD);
    Psyz_SpuFifoWrite(0xBEEF);
    zexpect_u32_eq(0x1004, Psyz_SpuGetTransferAddr());
    zexpect_u8_eq(0xAD, Psyz_SpuGetRam()[0x1000]);
    zexpect_u8_eq(0xDE, Psyz_SpuGetRam()[0x1001]);
    zexpect_u8_eq(0xEF, Psyz_SpuGetRam()[0x1002]);
    zexpect_u8_eq(0xBE, Psyz_SpuGetRam()[0x1003]);
}

ZTEST(spu, FifoWriteWrapsAtRamRange) {
    Psyz_SpuSetTransferAddr(PSYZ_SPU_RAM_SIZE - 2);
    Psyz_SpuFifoWrite(0xABCD);
    zexpect_u32_eq(0, Psyz_SpuGetTransferAddr());
    zexpect_u8_eq(0xCD, Psyz_SpuGetRam()[PSYZ_SPU_RAM_SIZE - 2]);
    zexpect_u8_eq(0xAB, Psyz_SpuGetRam()[PSYZ_SPU_RAM_SIZE - 1]);
}

ZTEST(spu, ResetClearsRamUnlessHot) {
    Psyz_SpuMemWrite(0x100, "ABCD", 4);
    Psyz_SpuReset(0);
    zexpect_u8_eq(0, Psyz_SpuGetRam()[0x100]);

    Psyz_SpuMemWrite(0x200, "WXYZ", 4);
    Psyz_SpuReset(1);
    zexpect_u8_eq('W', Psyz_SpuGetRam()[0x200]);
    zexpect_u8_eq('Z', Psyz_SpuGetRam()[0x203]);
}

ZTEST(spu, RegWriteXferFifoDepositsAndAdvances) {
    Psyz_SpuWrite(0x1A6, 0x0100);
    Psyz_SpuWrite(0x1A8, 0xCAFE);
    zexpect_u8_eq(0xFE, Psyz_SpuGetRam()[0x800]);
    zexpect_u8_eq(0xCA, Psyz_SpuGetRam()[0x801]);
    zexpect_u32_eq(0x802, Psyz_SpuGetTransferAddr());
}

ZTEST(spu, RegWritePureStorageRoundTrips) {
    Psyz_SpuWrite(0x050, 0x3FFF);
    zexpect_u16_eq(0x3FFF, Psyz_SpuRead(0x050));
    Psyz_SpuWrite(0x180, 0x4000);
    zexpect_u16_eq(0x4000, Psyz_SpuRead(0x180));
    Psyz_SpuWrite(0x1C0, 0x1234);
    zexpect_u16_eq(0x1234, Psyz_SpuRead(0x1C0));
}

ZTEST(spu, RegWriteBulkUploadViaFifoMatchesPayload) {
    unsigned char payload[256];
    unsigned char buf[256];
    size_t i;
    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (unsigned char)((i * 13) ^ 0xA5);
    }
    Psyz_SpuWrite(0x1A6, 0x0080);
    for (i = 0; i < sizeof(payload); i += 2) {
        Psyz_SpuWrite(
            0x1A8, (unsigned short)(payload[i] | (payload[i + 1] << 8)));
    }
    Psyz_SpuMemRead(0x400, buf, sizeof(buf));
    zexpect_u8array_eq(payload, buf, sizeof(payload));
    zexpect_u32_eq(0x400u + sizeof(payload), Psyz_SpuGetTransferAddr());
}

ZTEST(spu, MemWriteBulkDepositsAndAdvancesCursor) {
    unsigned char payload[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    Psyz_SpuSetTransferAddr(0x2000);
    Psyz_SpuFifoWriteBulk(payload, sizeof(payload));
    zexpect_u32_eq(0x2000u + sizeof(payload), Psyz_SpuGetTransferAddr());
    zexpect_u8array_eq(payload, &Psyz_SpuGetRam()[0x2000], sizeof(payload));
}

ZTEST(spu, BulkUploadViaFifoMatchesDirectMemWrite) {
    unsigned char payload[1024];
    unsigned char a[1024];
    unsigned char b[1024];
    size_t i;
    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (unsigned char)(i * 7 + 13);
    }
    Psyz_SpuSetTransferAddr(0x10000);
    for (i = 0; i < sizeof(payload); i += 2) {
        unsigned short w = (unsigned short)(payload[i] | (payload[i + 1] << 8));
        Psyz_SpuFifoWrite(w);
    }
    Psyz_SpuMemWrite(0x20000, payload, sizeof(payload));

    Psyz_SpuMemRead(0x10000, a, sizeof(a));
    Psyz_SpuMemRead(0x20000, b, sizeof(b));
    zexpect_u8array_eq(b, a, sizeof(a));
    zexpect_u8array_eq(payload, a, sizeof(payload));
}

// from PCSX Redux PR: https://github.com/grumpycoders/pcsx-redux/pull/2018
static const unsigned char kAdpcmSilent[64] = {
    0x00, 0x06, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x00, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char kAdpcmSine[64] = {
    0x00, 0x06, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc,
    0x89, 0x88, 0x88, 0xb9, 0xec, 0x00, 0x00, 0x10, 0x43, 0x76, 0x77,
    0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec, 0x00,
    0x00, 0x10, 0x43, 0x76, 0x77, 0x77, 0x46, 0x13, 0xe0, 0xbc, 0x89,
    0x88, 0x88, 0xb9, 0xec, 0x00, 0x03, 0x10, 0x43, 0x76, 0x77, 0x77,
    0x46, 0x13, 0xe0, 0xbc, 0x89, 0x88, 0x88, 0xb9, 0xec,
};
static const unsigned char kAdpcmTriangle[64] = {
    0x00, 0x06, 0x10, 0x32, 0x54, 0x76, 0x56, 0x34, 0x12, 0xe0, 0xcd,
    0xab, 0x89, 0xa9, 0xcb, 0xed, 0x00, 0x00, 0x10, 0x32, 0x54, 0x76,
    0x56, 0x34, 0x12, 0xe0, 0xcd, 0xab, 0x89, 0xa9, 0xcb, 0xed, 0x00,
    0x00, 0x10, 0x32, 0x54, 0x76, 0x56, 0x34, 0x12, 0xe0, 0xcd, 0xab,
    0x89, 0xa9, 0xcb, 0xed, 0x00, 0x03, 0x10, 0x32, 0x54, 0x76, 0x56,
    0x34, 0x12, 0xe0, 0xcd, 0xab, 0x89, 0xa9, 0xcb, 0xed,
};
static const unsigned char kAdpcmSquare[64] = {
    0x00, 0x06, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x88, 0x88,
    0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0x00, 0x77, 0x77, 0x77, 0x77,
    0x77, 0x77, 0x77, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00,
    0x00, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x88, 0x88, 0x88,
    0x88, 0x88, 0x88, 0x88, 0x00, 0x03, 0x77, 0x77, 0x77, 0x77, 0x77,
    0x77, 0x77, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
};
static const unsigned char kAdpcmSine394Hz[64] = {
    0x00, 0x06, 0x00, 0x10, 0x21, 0x32, 0x33, 0x44, 0x54, 0x55, 0x66,
    0x76, 0x77, 0x77, 0x77, 0x77, 0x00, 0x00, 0x77, 0x77, 0x77, 0x77,
    0x77, 0x66, 0x56, 0x55, 0x44, 0x34, 0x33, 0x22, 0x11, 0x00, 0x00,
    0x00, 0xf0, 0xef, 0xde, 0xcd, 0xcc, 0xbb, 0xab, 0xaa, 0x99, 0x89,
    0x88, 0x88, 0x88, 0x88, 0x00, 0x03, 0x88, 0x88, 0x88, 0x88, 0x88,
    0x99, 0xa9, 0xaa, 0xbb, 0xcb, 0xcc, 0xdd, 0xee, 0xff,
};
static const unsigned char kAdpcmSine5512Hz[64] = {
    0x00, 0x06, 0x50, 0x57, 0xa0, 0xa8, 0x50, 0x57, 0xa0, 0xa8, 0x50,
    0x57, 0xa0, 0xa8, 0x50, 0x57, 0x00, 0x00, 0xa0, 0xa8, 0x50, 0x57,
    0xa0, 0xa8, 0x50, 0x57, 0xa0, 0xa8, 0x50, 0x57, 0xa0, 0xa8, 0x00,
    0x00, 0x50, 0x57, 0xa0, 0xa8, 0x50, 0x57, 0xa0, 0xa8, 0x50, 0x57,
    0xa0, 0xa8, 0x50, 0x57, 0x00, 0x03, 0xa0, 0xa8, 0x50, 0x57, 0xa0,
    0xa8, 0x50, 0x57, 0xa0, 0xa8, 0x50, 0x57, 0xa0, 0xa8,
};

enum {
    kSampleAddr = 0x1080,
    // One full voice-1 capture ring: 512 shorts mirrored to SPU RAM at 0x0800.
    kCaptureBytes = 1024,
};

// Envelope set with voice at full peak from sample 0
static void setup_voice1(unsigned int spu_addr, unsigned short pitch) {
    const unsigned int voice = 1;
    const unsigned int base = voice << 4;
    Psyz_SpuWrite(base + 0x04, pitch);
    Psyz_SpuWrite(base + 0x06, (unsigned short)(spu_addr >> 3));
    Psyz_SpuWrite(base + 0x08, 0x000f); // instant attack, sustain=0xF
    Psyz_SpuWrite(base + 0x0A, 0x1fc0); // sustain rate=0x7F linear
    Psyz_SpuWrite(base + 0x0E, (unsigned short)(spu_addr >> 3));
}

static void pull_samples_nop(int nframes) {
    short* scratch = (short*)calloc((size_t)nframes * 2, sizeof(short));
    Psyz_SpuPullSamples(scratch, nframes);
    free(scratch);
}

// SPU enabled, all voices off, reverb off. Used before each ADPCM scenario so
// the capture ring starts clean.
static void spu_reset_quiet(void) {
    Psyz_SpuReset(0);
    Psyz_SpuWrite(0x1AA, 0);      // SPU_CTRL: disable
    Psyz_SpuWrite(0x180, 0);      // SPU_VOL_MAIN_LEFT
    Psyz_SpuWrite(0x182, 0);      // SPU_VOL_MAIN_RIGHT
    Psyz_SpuWrite(0x184, 0);      // SPU_REVERB_LEFT
    Psyz_SpuWrite(0x186, 0);      // SPU_REVERB_RIGHT
    Psyz_SpuWrite(0x18C, 0xFFFF); // KEY_OFF_LOW
    Psyz_SpuWrite(0x18E, 0xFFFF); // KEY_OFF_HIGH
    Psyz_SpuWrite(0x190, 0);      // PITCH_MOD_LOW
    Psyz_SpuWrite(0x192, 0);      // PITCH_MOD_HIGH
    Psyz_SpuWrite(0x194, 0);      // NOISE_EN_LOW
    Psyz_SpuWrite(0x196, 0);      // NOISE_EN_HIGH
    Psyz_SpuWrite(0x198, 0);      // REVERB_EN_LOW
    Psyz_SpuWrite(0x19A, 0);      // REVERB_EN_HIGH
    Psyz_SpuWrite(0x1A2, 0xFFFF); // REVERB_ADDR
    Psyz_SpuWrite(0x1AA, 0x8000); // SPU_CTRL: enable
}

static void spu_voice1_keyon(unsigned int spuAddr, unsigned short pitch) {
    setup_voice1(spuAddr, pitch);
    Psyz_SpuWrite(0x18C, 0);       // KEY_OFF_LOW
    Psyz_SpuWrite(0x18E, 0);       // KEY_OFF_HIGH
    Psyz_SpuWrite(0x188, 1u << 1); // KEY_ON voice 1
}

static void run_voice1_with_envelope(
    const unsigned char* sample64, unsigned short pitch, unsigned short adsr_lo,
    unsigned char out_capture[kCaptureBytes]) {
    unsigned char upload[128];
    spu_reset_quiet();
    memcpy(upload, sample64, 64);
    memset(upload + 64, 0xAA, 64);
    Psyz_SpuMemWrite(kSampleAddr, upload, sizeof(upload));
    Psyz_SpuWrite(0x1AA, 0x8000 | 0x4000); // SPU enable + unmute
    Psyz_SpuWrite(0x180, 0x3FFF);
    Psyz_SpuWrite(0x182, 0x3FFF);

    setup_voice1(kSampleAddr, pitch);
    Psyz_SpuWrite((1 << 4) + 0x08, adsr_lo);
    Psyz_SpuWrite(0x18C, 0);       // KEY_OFF_LOW
    Psyz_SpuWrite(0x18E, 0);       // KEY_OFF_HIGH
    Psyz_SpuWrite(0x188, 1u << 1); // KEY_ON voice 1
    pull_samples_nop(512);

    Psyz_SpuMemRead(0x0800, out_capture, kCaptureBytes);
    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);
}

static void run_voice1_with_sample(
    const unsigned char* sample64, unsigned short pitch,
    unsigned char out_capture[kCaptureBytes]) {
    run_voice1_with_envelope(sample64, pitch, 0x000f, out_capture);
}

// Returns a malloc'd buffer (NULL with *size 0 when the file cannot be opened).
static unsigned char* load_expected(
    const char* name, const char* ext, const char* what, size_t* size) {
    char path[256];
    FILE* f;
    long n;
    unsigned char* buf;
    snprintf(path, sizeof(path), "expected/spu/%s%s", name, ext);
    *size = 0;
    f = fopen(path, "rb");
    if (!f) {
        zfail("cannot open %s: %s", what, path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char*)calloc(n > 0 ? (size_t)n : 1, 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        zfail("short read: %s", path);
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

enum { SampleTolerance = 1500 };

static bool samples_close(
    const unsigned char* a, const unsigned char* b, unsigned bytes) {
    unsigned i;
    for (i = 0; i + 1 < bytes; i += 2) {
        short av = (short)(a[i] | (a[i + 1] << 8));
        short bv = (short)(b[i] | (b[i + 1] << 8));
        int d = av - bv;
        if (d < -SampleTolerance || d > SampleTolerance) {
            return false;
        }
    }
    return true;
}

typedef struct {
    uint32_t magic;  // 'PCMT' header magic code
    uint32_t length; // size of this header, for versioning or expandability
    uint32_t warmup; // ADPCM-decoded bytes with artifacts from previous run
    uint32_t period; // repeatable ADPCM-decoded bytes after warm-up
} PcmTestHeader;

// Same warmup-skipping period-locating algorithm as pcsx-redux's
// spu_compare_golden(): the warmup prefix is non-deterministic (depends on
// the pipeline's state at KEY_ON) so we slide the period over the capture
// looking for a tolerance-close match, then verify periodicity from there.
static int compare_golden(const char* name, const unsigned char* cap,
                          const unsigned char* gold, size_t gold_size) {
    PcmTestHeader h;
    const unsigned char* period_start;
    uint32_t found_at = 0xFFFFFFFFu;
    uint32_t off, i;
    if (gold_size < sizeof(PcmTestHeader)) {
        zprintf("%s: golden too small\n", name);
        return 0;
    }
    memcpy(&h, gold, sizeof(h));
    if (h.magic != 0x544D4350u) {
        zprintf("%s: bad magic 0x%x\n", name, h.magic);
        return 0;
    }
    if (h.period == 0 || h.period > 1024) {
        zprintf("%s: bad period %u\n", name, h.period);
        return 0;
    }
    period_start = gold + h.length + h.warmup;

    for (off = 0; off + h.period <= 1024; off += 2) {
        if (samples_close(cap + off, period_start, h.period)) {
            found_at = off;
            break;
        }
    }
    if (found_at == 0xFFFFFFFFu) {
        zprintf("%s: period not found in capture (tol=%d)\n", name,
                SampleTolerance);
        return 0;
    }
    for (i = found_at; i + 1 < 1024; i += 2) {
        uint32_t base = (i - found_at) % h.period;
        short av = (short)(cap[i] | (cap[i + 1] << 8));
        short ev = (short)(period_start[base] | (period_start[base + 1] << 8));
        int d = av - ev;
        if (d < -SampleTolerance || d > SampleTolerance) {
            zprintf("%s: periodicity broken at sample %u: got %d, want %d "
                    "(found_at=%u)\n",
                    name, i / 2, av, ev, found_at);
            return 0;
        }
    }
    return 1;
}

static int check_golden(const char* name, const unsigned char* cap) {
    size_t size;
    unsigned char* gold =
        load_expected(name, ".test.pcm", "expected PCM file", &size);
    int ok = compare_golden(name, cap, gold, size);
    free(gold);
    return ok;
}

#define SPU_EXPECT_GOLDEN(name, cap)                                           \
    zexpect_s32_eq(1, check_golden(#name, (cap)))

#ifdef __psyz
ZTEST(spu, GaussianKeyOnPitchPhase) {
    // Independent SPU replay: Beetle PSX 9164674, with its frontend-only
    // 75% headroom attenuation removed. Fractional Gaussian products are
    // summed before shifting, as in the hardware-validated FPGA core.
    static const unsigned char block[16] = {
        0,    7,    0x10, 0x32, 0x54, 0x76, 0x77, 0x65,
        0x43, 0x21, 0xef, 0xcd, 0xab, 0x89, 0x88, 0x9a};
    static const unsigned pitch[] = {0x800, 0x1234, 0x2000};
    static const short reference[3][32] = {
        {0,     0,     0,     0,     0,     0,     2678,  7144,
         10204, 12244, 14284, 16324, 18362, 20402, 22442, 24482,
         26469, 27954, 28502, 28555, 28448, 27337, 24514, 22205,
         22378, 22041, 18331, 14644, 14275, 13886, 10176, 6487},
        {0,      0,      0,      0,      0,      0,      3815,   11690,
         17996,  22650,  27123,  28553,  27491,  22063,  20623,  14153,
         10069,  6260,   -997,   -7866,  -12503, -17155, -21793, -26430,
         -30952, -32626, -31812, -26232, -21237, 243,    5884,   10521},
        {0,      0,      0,      0,     0,      0,      5356,   17852,
         27954,  27337,  22047,  13889, 5730,   -8168,  -16328, -24486,
         -32038, -31421, -23696, 4084,  12244,  20402,  27954,  27337,
         22041,  13886,  5729,   -8166, -16324, -24480, -32030, -31413},
    };
    for (unsigned pass = 0; pass < 3; ++pass) {
        short pcm[64];
        Psyz_SpuReset(0);
        Psyz_SpuMemWrite(0x1000, block, sizeof(block));
        Psyz_SpuWrite(0x1AA, 0xC000);
        Psyz_SpuWrite(0x180, 0x3FFF);
        Psyz_SpuWrite(0x182, 0x3FFF);
        Psyz_SpuWrite(0x10, 0x3FFF);
        Psyz_SpuWrite(0x12, 0x3FFF);
        Psyz_SpuWrite(0x14, pitch[pass]);
        Psyz_SpuWrite(0x16, 0x200);
        Psyz_SpuWrite(0x18, 0x00FF);
        Psyz_SpuWrite(0x1A, 0x1FC0);
        Psyz_SpuWrite(0x1E, 0x200);
        Psyz_SpuWrite(0x188, 2);
        Psyz_SpuPullSamples(pcm, 32);
        for (unsigned frame = 0; frame < 32; ++frame) {
            zexpect_s32_eq(reference[pass][frame], pcm[frame * 2]);
            zexpect_s32_eq(pcm[frame * 2], pcm[frame * 2 + 1]);
        }
    }
}
#endif

#ifdef __psyz
ZTEST(spu, GaussianFmReference) {
    // Independent Beetle PSX 9164674 output without frontend attenuation.
    // Check sustain, key-off, retrigger and clipping. Spaced exact windows
    // catch errors that accumulate FM carrier phase over many samples.
    static const unsigned char block[16] = {
        0,    7,    0x10, 0x32, 0x54, 0x76, 0x77, 0x65,
        0x43, 0x21, 0xef, 0xcd, 0xab, 0x89, 0x88, 0x9a};
    static const unsigned starts[4][6] = {
        {0, 256, 8192, 32768, 65536, 88184},
        {29998, 30016, 30128, 32768, 65536, 88184},
        {29998, 30016, 30128, 32768, 65536, 88184},
        {0, 256, 8192, 32768, 65536, 88184}};
    static const short reference[4][6][16] = {
        {
            {0, 0, 0, 0, 0, 0, 5140, 17768, 28513, 22290, 9104, -8376, -22718,
             -32639, -21173, 9105},
            {-32615, -23131, 10457, 24636, 26380, 14116, 5729, -10286, -19397,
             -27126, -32221, -32613, -30309, -26602, -26131, -26217},
            {1808, 5645, 8099, 9485, 9820, 9852, 10171, 11731, 14902, 22835,
             28547, 22155, 13269, -4111, -18234, -31973},
            {6140, -12357, -26679, -32085, -4117, 11142, 22054, 28530, 23112,
             20085, 14367, 9243, 5977, 6186, 5479, 5378},
            {14125, 13338, 8297, 6303, 6092, 6078, 5979, 6223, 3089, -9489,
             -18633, -28862, -31942, -5523, 13038, 27154},
            {-10589, -11990, -14030, -21229, -30069, -32037, -13761, 10155,
             23345, 26380, 14433, -3670, -17342, -29337, -31798, -20088},
        },
        {
            {6960, 6250, 5203, 3718, 3584, -4788, -12852, -20513, -28178,
             -32628, -27584, -13027, 5821, 13485, 21147, 28066},
            {14433, 6205, -5833, -13632, -21294, -28958, -32630, -26686, -8643,
             6602, 14265, 21926, 28332, 26715, 21992, 14229},
            {-20676, 4386, 12050, 19712, 27191, 28357, 22333, 14135, 5971,
             -3727, -12198, -19859, -27524, -32612, -28567, -16731},
            {19569, 27078, 28395, 22277, 14118, 6004, -3439, -12054, -19716,
             -27380, -32605, -28790, -17596, 5008, 12672, 20350},
            {-7333, -15002, -22664, -30294, -32609, -26105, -2514, 7971, 15635,
             23297, 28522, 24707, 19719, 12550, 5203, -8119},
            {-25802, -32357, -31049, -24146, 3425, 11094, 18756, 26390, 28515,
             22048, 14283, 6503, -1805, -11241, -18903, -26583},
        },
        {
            {6960, 6250, 5203, 3718, -8931, -16594, -24257, -31609, -32275,
             -25497, 5518, 17386, 28471, 22428, 6008, -11640},
            {-25765, 4928, 14059, 21481, 27141, 28547, 28101, 25278, 23112,
             22649, 22618, 22430, 22084, 22242, 14338, 5479},
            {-18596, 5709, 15349, 26063, 25835, 14224, -1805, -17916, -31703,
             -26260, 4482, 15460, 25600, 28433, 22186, 15684},
            {-31299, -26111, 6060, 19011, 28459, 22064, 14121, 6041, -2791,
             -9266, -13409, -16466, -18441, -19333, -19413, -19445},
            {-11385, -13855, -15225, -15559, -15591, -15910, -17470, -20673,
             -28607, -32620, -25710, 6283, 18676, 28546, 19337, 6052},
            {-30978, -32246, -31812, -22856, 6123, 17163, 28241, 22248, 7050,
             -9808, -23620, -32631, -24940, 6458, 15970, 25050},
        },
        {
            {0, 0, 0, 0, 0, 0, 6527, 23551, 22049, 5972, -13189, -29503, -26118,
             9105, 25419, 22049},
            {25030, 10933, -9872, -26185, -29113, 5789, 21306, 28012, 14890,
             6241, -6872, -13775, -19110, -22919, -25182, -25930},
            {15525, 13556, 6853, 6015, 6140, 6152, 6252, 4728, -4858, -16450,
             -29384, -27158, 6841, 23154, 23458, 8591},
            {7956, 24270, 22318, 6714, -12022, -27954, -27761, 3208, 15396,
             24397, 28544, 26181, 22088, 22132, 20278, 20020},
            {-12819, -17852, -21325, -23285, -23763, -23811, -24257, -26456,
             -30856, -29630, 1784, 16542, 28548, 14726, -2726, -20322},
            {22436, 24413, 27141, 26380, 14115, -227, -18505, -32592, -13303,
             14425, 28509, 18007, 1558, -18505, -32507, -26077},
        },
    };
    for (unsigned pass = 0; pass < 4; ++pass) {
        Psyz_SpuReset(0);
        Psyz_SpuMemWrite(0x1000, block, sizeof(block));
        Psyz_SpuWrite(0x1AA, 0xC000);
        Psyz_SpuWrite(0x180, 0x3FFF);
        Psyz_SpuWrite(0x182, 0x3FFF);
        Psyz_SpuWrite(0x184, 0);
        Psyz_SpuWrite(0x186, 0);
        Psyz_SpuWrite(0x190, 2);
        Psyz_SpuWrite(0x192, 0);
        Psyz_SpuWrite(0x194, 0);
        Psyz_SpuWrite(0x196, 0);
        Psyz_SpuWrite(0x198, 0);
        Psyz_SpuWrite(0x19A, 0);
        for (unsigned voice = 0; voice < 2; ++voice) {
            unsigned base = voice * 16;
            Psyz_SpuWrite(base, voice ? 0x3FFF : 0);
            Psyz_SpuWrite(base + 2, voice ? 0x3FFF : 0);
            Psyz_SpuWrite(
                base + 4, voice ? (pass == 3 ? 0x2A88 : 0x1E12) : 0x1234);
            Psyz_SpuWrite(base + 6, 0x200);
            Psyz_SpuWrite(base + 8, 0x00FF);
            Psyz_SpuWrite(base + 10, 0x1FC0);
            Psyz_SpuWrite(base + 14, 0x200);
        }
        Psyz_SpuWrite(0x188, 3);
        for (unsigned cursor = 0; cursor < 88200;) {
            unsigned frames = 88200 - cursor;
            short pcm[256];
            if (pass == 1 && cursor == 30000) {
                Psyz_SpuWrite(0x18, 0x0CFF);
                Psyz_SpuWrite(0x18C, 1); // release the muted modulator only
            }
            if (pass == 2 && cursor == 30000)
                Psyz_SpuWrite(0x188, 1); // retrigger the active modulator only
            if (frames > 128)
                frames = 128;
            if (pass && cursor < 30000 && frames > 30000 - cursor)
                frames = 30000 - cursor;
            Psyz_SpuPullSamples(pcm, (int)frames);
            for (unsigned frame = 0; frame < frames; ++frame)
                for (unsigned window = 0; window < 6; ++window)
                    if (cursor + frame >= starts[pass][window] &&
                        cursor + frame < starts[pass][window] + 16) {
                        short expected =
                            reference[pass][window]
                                     [cursor + frame - starts[pass][window]];
                        zexpect_s32_eq(expected, pcm[frame * 2]);
                        zexpect_s32_eq(expected, pcm[frame * 2 + 1]);
                    }
            cursor += frames;
        }
    }
}
#endif

#ifdef __psyz
ZTEST(spu, AdpcmOneShotEndPipeline) {
    // Independently observed END timing from Beetle PSX 9164674. Decoding
    // runs ahead of playback; the final queued samples must not leak out.
    static const unsigned pitches[7] = {
        0x400, 0x800, 0x1000, 0x1234, 0x2000, 0x3000, 0x4000};
    static const unsigned stops[2][7] = {
        {77, 41, 23, 21, 14, 11, 10},
        {189, 97, 51, 46, 28, 21, 17},
    };
    for (unsigned blocks = 1; blocks <= 2; ++blocks)
        for (unsigned pass = 0; pass < 7; ++pass) {
            unsigned char data[32];
            short pcm[520];
            memset(data, 0x77, sizeof(data));
            data[0] = 0;
            data[1] = blocks == 1 ? 1 : 0;
            data[16] = 0;
            data[17] = 1;
            Psyz_SpuReset(0);
            Psyz_SpuMemWrite(0x1000, data, blocks * 16);
            Psyz_SpuWrite(0x1AA, 0xC000);
            Psyz_SpuWrite(0x180, 0x3FFF);
            Psyz_SpuWrite(0x182, 0x3FFF);
            Psyz_SpuWrite(0x184, 0);
            Psyz_SpuWrite(0x186, 0);
            Psyz_SpuWrite(0x190, 0);
            Psyz_SpuWrite(0x192, 0);
            Psyz_SpuWrite(0x194, 0);
            Psyz_SpuWrite(0x196, 0);
            Psyz_SpuWrite(0x10, 0x3FFF);
            Psyz_SpuWrite(0x12, 0x3FFF);
            Psyz_SpuWrite(0x14, pitches[pass]);
            Psyz_SpuWrite(0x16, 0x200);
            Psyz_SpuWrite(0x18, 0x00FF);
            Psyz_SpuWrite(0x1A, 0x1FC0);
            Psyz_SpuWrite(0x1E, 0x200);
            Psyz_SpuWrite(0x188, 2);
            Psyz_SpuPullSamples(pcm, 260);
            unsigned stop = stops[blocks - 1][pass];
            zexpect_s32_ne(0, pcm[(stop - 1) * 2]);
            for (unsigned frame = stop; frame < 260; ++frame) {
                zexpect_s32_eq(0, pcm[frame * 2]);
                zexpect_s32_eq(0, pcm[frame * 2 + 1]);
            }
            zexpect_u16_eq(0, Psyz_SpuRead(0x1C));
        }
    // NON replaces ADPCM and must keep the envelope running through END.
    short pcm[520];
    Psyz_SpuReset(0);
    const unsigned char end_block[16] = {0, 1};
    Psyz_SpuMemWrite(0x1000, end_block, sizeof(end_block));
    Psyz_SpuWrite(0x1AA, 0xFF00);
    Psyz_SpuWrite(0x14, 0x1000);
    Psyz_SpuWrite(0x194, 2);
    Psyz_SpuWrite(0x188, 2);
    Psyz_SpuPullSamples(pcm, 260);
    zexpect_s32_ne(0, Psyz_SpuRead(0x1C));
}
#endif

ZTEST(spu, adpcm_decode_silent) {
    // A silent ADPCM payload should produce a capture ring full of zeros
    unsigned char cap[1024];
    int i;
    run_voice1_with_sample(kAdpcmSilent, 0x1000, cap);
    for (i = 0; i < 1024; i++) {
        if (cap[i] != 0) {
            zprintf("byte %d\n", i);
        }
        zassert_u8_eq(0, cap[i]);
    }
}

ZTEST(spu, adpcm_decode_sinewave) {
    unsigned char cap[1024];
    run_voice1_with_sample(kAdpcmSine, 0x1000, cap);
    SPU_EXPECT_GOLDEN(sine, cap);
}

ZTEST(spu, adpcm_decode_sinewave_full_envelope) {
    unsigned char cap[1024];
    run_voice1_with_envelope(kAdpcmSine, 0x1000, 0x00ff, cap);
    SPU_EXPECT_GOLDEN(sine_full, cap);
}

ZTEST(spu, adpcm_decode_sinewave_lowpitch) {
    unsigned char cap[1024];
    run_voice1_with_sample(kAdpcmSine394Hz, 0x1000, cap);
    SPU_EXPECT_GOLDEN(sine_low, cap);
}

ZTEST(spu, adpcm_decode_sinewave_highpitch) {
    unsigned char cap[1024];
    run_voice1_with_sample(kAdpcmSine5512Hz, 0x1000, cap);
    SPU_EXPECT_GOLDEN(sine_high, cap);
}

ZTEST(spu, adpcm_decode_trianglewave) {
    unsigned char cap[1024];
    run_voice1_with_sample(kAdpcmTriangle, 0x1000, cap);
    SPU_EXPECT_GOLDEN(triangle, cap);
}

ZTEST(spu, adpcm_decode_squarewave) {
    unsigned char cap[1024];
    run_voice1_with_sample(kAdpcmSquare, 0x1000, cap);
    SPU_EXPECT_GOLDEN(square, cap);
}

ZTEST(spu, adpcm_decode_with_loop) {
    // Captures one full lap of triangle output. Then we key off, let the ring
    // drain, key on with the same payload and capture again.
    unsigned char cap[1024];
    run_voice1_with_sample(kAdpcmTriangle, 0x1000, cap);
    SPU_EXPECT_GOLDEN(loop_t0, cap);

    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);
    pull_samples_nop(512);

    spu_voice1_keyon(kSampleAddr, 0x1000);
    pull_samples_nop(512);
    Psyz_SpuMemRead(0x0800, cap, sizeof(cap));
    SPU_EXPECT_GOLDEN(loop_t1, cap);

    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);
}

// For pitch changes during voice on, enable vibrato or bends.
// This is used during the first five notes on FF7 Main Theme intro
ZTEST(spu, ChangePitchWhileVoiceIsOn) {
    // self-looping ADPCM on block 0
    unsigned char payload[32];
    unsigned char zeros[1024] = {0};
    unsigned char cap_const[1024];
    unsigned char cap_changed[1024];
    const unsigned int base = 1u << 4; // voice 1 register base
    int i;
    memset(payload, 0, sizeof(payload));
    payload[1] = 0x04;  // block 0: loop-start
    payload[17] = 0x03; // block 1: loop-end + repeat
    for (i = 0; i < 14; i++)
        payload[18 + i] = 0x77;

    spu_reset_quiet();
    Psyz_SpuWrite(0x1AA, 0x8000 | 0x4000);
    Psyz_SpuWrite(0x180, 0x3FFF);
    Psyz_SpuWrite(0x182, 0x3FFF);
    Psyz_SpuMemWrite(kSampleAddr, payload, sizeof(payload));
    spu_voice1_keyon(kSampleAddr, 0x0800); // set ADPCM pitch at 50%
    pull_samples_nop(256);
    Psyz_SpuMemWrite(0x0800, zeros, sizeof(zeros));
    pull_samples_nop(512);
    Psyz_SpuMemRead(0x0800, cap_const, sizeof(cap_const));
    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);

    // modify pitch without resetting the voice key
    spu_reset_quiet();
    Psyz_SpuWrite(0x1AA, 0x8000 | 0x4000);
    Psyz_SpuWrite(0x180, 0x3FFF);
    Psyz_SpuWrite(0x182, 0x3FFF);
    Psyz_SpuMemWrite(kSampleAddr, payload, sizeof(payload));
    spu_voice1_keyon(kSampleAddr, 0x0800);
    pull_samples_nop(256);
    Psyz_SpuWrite(base + 0x04, 0x2000); // set ADPCM pitch at 200%
    Psyz_SpuMemWrite(0x0800, zeros, sizeof(zeros));
    pull_samples_nop(512);
    Psyz_SpuMemRead(0x0800, cap_changed, sizeof(cap_changed));
    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);

    // if done correctly, the two captures will differ; it's very hard to test
    // byte-by-byte here due to how gauss interpolation works, a memcpy will do
    if (!zexpect_u8array_ne(cap_const, cap_changed, sizeof(cap_const))) {
        zprintf("mid-playback pitch write had no effect on voice output\n");
    }
}

// Peak absolute amplitude of the left and right channels over `nframes` of the
// final stereo mix that Psyz_SpuPullSamples produces (NOT the per-voice capture
// buffer, which is pre-volume). out_l/out_r receive the per-channel peaks.
static void mix_peak(int nframes, int* out_l, int* out_r) {
    short* buf = (short*)calloc((size_t)nframes * 2, sizeof(short));
    int pl = 0, pr = 0;
    int i;
    Psyz_SpuPullSamples(buf, nframes);
    for (i = 0; i < nframes; i++) {
        int l = abs((int)buf[i * 2]);
        int r = abs((int)buf[i * 2 + 1]);
        if (l > pl)
            pl = l;
        if (r > pr)
            pr = r;
    }
    free(buf);
    *out_l = pl;
    *out_r = pr;
}

// given a sine wave and L/R volume, get out peak volume out for L/R capture
static void voice1_volume_peak(
    unsigned short vol_l, unsigned short vol_r, int* peak_l, int* peak_r) {
    const unsigned int base = 1u << 4; // voice 1 register base
    spu_reset_quiet();
    Psyz_SpuMemWrite(kSampleAddr, kAdpcmSine, sizeof(kAdpcmSine));
    Psyz_SpuWrite(0x1AA, 0x8000 | 0x4000); // SPU enable + unmute
    Psyz_SpuWrite(0x180, 0x3FFF);          // main volume left = unity
    Psyz_SpuWrite(0x182, 0x3FFF);          // main volume right = unity
    Psyz_SpuWrite(base + 0x00, vol_l);     // voice 1 volume left
    Psyz_SpuWrite(base + 0x02, vol_r);     // voice 1 volume right
    spu_voice1_keyon(kSampleAddr, 0x1000);
    // Skip the key-on envelope delay, then measure a steady window.
    pull_samples_nop(32);
    mix_peak(256, peak_l, peak_r);
    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);
}

ZTEST(spu, VoiceVolumePansLeftAndRight) {
    int l, r;

    voice1_volume_peak(0x3FFF, 0x0000, &l, &r);
    if (!zexpect_s32_gt(1000, l)) {
        zprintf("hard-left voice produced no left output\n");
    }
    if (!zexpect_s32_eq(0, r)) {
        zprintf("hard-left voice leaked into the right channel\n");
    }

    voice1_volume_peak(0x0000, 0x3FFF, &l, &r);
    if (!zexpect_s32_gt(1000, r)) {
        zprintf("hard-right voice produced no right output\n");
    }
    if (!zexpect_s32_eq(0, l)) {
        zprintf("hard-right voice leaked into the left channel\n");
    }
}

ZTEST(spu, VoiceVolumeScalesAmplitude) {
    int full_l, full_r, half_l, half_r;
    double ratio;
    voice1_volume_peak(0x3FFF, 0x3FFF, &full_l, &full_r);
    voice1_volume_peak(0x2000, 0x2000, &half_l, &half_r);

    zassert_s32_gt(0, full_l);
    // 0x2000 / 0x3FFF ~= 0.5; allow a generous band for rounding and the
    // gauss-interpolated sample peak landing on different frames.
    ratio = (double)half_l / (double)full_l;
    if (!zexpect_s32_ne(0, ratio > 0.35)) {
        zprintf("half volume too quiet (ratio %f)\n", ratio);
    }
    if (!zexpect_s32_ne(0, ratio < 0.65)) {
        zprintf("half volume too loud (ratio %f)\n", ratio);
    }
}

ZTEST(spu, VoiceVolumeZeroIsSilent) {
    int l, r;
    voice1_volume_peak(0x0000, 0x0000, &l, &r);
    zexpect_s32_eq(0, l);
    zexpect_s32_eq(0, r);
}

ZTEST(spu, KeyOnLatchesStartAddrAndActivates) {
    short cap[28];
    short ring[512];
    int nonzero = 0;
    int i;
    Psyz_SpuMemWrite(kSampleAddr, kAdpcmSine, sizeof(kAdpcmSine));
    setup_voice1(kSampleAddr, 0x1000);
    // Before KEY_ON: voice 1 capture region stays zero through a pull.
    pull_samples_nop(28);
    Psyz_SpuMemRead(0x0800, cap, sizeof(cap));
    for (i = 0; i < 28; i++) {
        if (!zexpect_s16_eq(0, cap[i])) {
            zprintf("pre-keyon sample %d\n", i);
        }
    }
    // KEY_ON, then pull. The capture_pos has advanced 28*2=56 bytes from
    // the first pull, so new samples land at 0x0800+56. Read the whole
    // ring (512 shorts = 1024 bytes) and assert *some* sample is non-zero.
    Psyz_SpuWrite(0x188, 1u << 1);
    pull_samples_nop(28);
    Psyz_SpuMemRead(0x0800, ring, sizeof(ring));
    for (i = 0; i < 512; i++) {
        if (ring[i] != 0)
            nonzero++;
    }
    zexpect_s32_gt(0, nonzero);
}

ZTEST(spu, KeyOffSilencesVoice) {
    // Capture taps use the current envelope; the updated ENVX applies to
    // the next sample. Permit that final current sample before checking the
    // following silent capture writes.
    unsigned char zeros[128] = {0};
    short cap[64];
    int i;
    Psyz_SpuMemWrite(kSampleAddr, kAdpcmSine, sizeof(kAdpcmSine));
    setup_voice1(kSampleAddr, 0x1000);
    Psyz_SpuWrite(0x188, 1u << 1);
    pull_samples_nop(56);
    Psyz_SpuWrite(0x18C, 1u << 1); // KEY_OFF voice 1
    pull_samples_nop(2); // latch key-off, then finish the release clock
    zexpect_u16_eq(0, Psyz_SpuRead(0x1C));
    // Zero out the capture region so we observe only post-keyoff writes.
    Psyz_SpuMemWrite(0x0800, zeros, sizeof(zeros));
    pull_samples_nop(64);
    Psyz_SpuMemRead(0x0800, cap, sizeof(cap));
    for (i = 0; i < 64; i++) {
        if (!zexpect_s16_eq(0, cap[i])) {
            zprintf("post-keyoff sample %d\n", i);
        }
    }
}

ZTEST(spu, Bit11TogglesEveryHalfCaptureRing) {
    unsigned short s0, s1, s2;
    s0 = Psyz_SpuRead(0x1AE) & 0x800;
    pull_samples_nop(256);
    s1 = Psyz_SpuRead(0x1AE) & 0x800;
    zexpect_u16_ne(s0, s1);
    pull_samples_nop(256);
    s2 = Psyz_SpuRead(0x1AE) & 0x800;
    zexpect_u16_eq(s0, s2);
}

ZTEST(spu, AdpcmLoopRepeatJumpsToLoopAddr) {
    // Two blocks: block 0 = silent (flag=0x04 = loop-start marker);
    // block 1 = non-zero (flag=0x03 = loop-end + repeat). After exhausting
    // block 1 the voice must jump back to block 0 (not stop). With the
    // gauss pipeline the first ~3 samples are pre-fill; thereafter the
    // capture should contain non-zero samples (from block 1) and the voice
    // must remain active across multiple lap-equivalent durations.
    unsigned char payload[32];
    unsigned char zeros[1024] = {0};
    short ring[512];
    int nonzero = 0;
    int still_running = 0;
    int i;
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x00;
    payload[1] = 0x04; // block 0: silent, loop-start
    payload[16] = 0x00;
    payload[17] = 0x03;
    for (i = 0; i < 14; i++)
        payload[16 + 2 + i] = 0x77;

    Psyz_SpuMemWrite(kSampleAddr, payload, sizeof(payload));
    setup_voice1(kSampleAddr, 0x1000);
    Psyz_SpuWrite(0x188, 1u << 1);

    pull_samples_nop(300); // pull as many samples necessary to trigger loop
    Psyz_SpuMemRead(0x0800, ring, sizeof(ring));
    for (i = 0; i < 300; i++)
        if (ring[i] != 0)
            nonzero++;
    if (!zexpect_s32_gt(14, nonzero)) {
        zprintf("block 1 decoded samples never reached capture\n");
    }

    // Verify voice keeps producing output
    Psyz_SpuMemWrite(0x0800, zeros, sizeof(zeros));
    pull_samples_nop(300);
    Psyz_SpuMemRead(0x0800, ring, sizeof(ring));
    for (i = 0; i < 512; i++)
        if (ring[i] != 0)
            still_running++;
    if (!zexpect_s32_gt(0, still_running)) {
        zprintf("voice stopped instead of looping\n");
    }
}

#define ADSR_ATTACK(step, shift, exp)                                          \
    ((((step) & 3) << 8) | (((shift) & 31) << 10) | (!!(exp) << 15))
#define ADSR_DECAY(shift) (((shift) & 15) << 4)
#define ADSR_SUSTAIN(step, shift, level, direction, exp)                       \
    ((((step) & 3) << 22) | (((shift) & 31) << 24) | (((level) & 15) << 0) |   \
     (!!(direction) << 30) | ((uint32_t)!!(exp) << 31))
#define ADSR_RELEASE(shift, exp) ((((shift) & 31) << 16) | (!!(exp) << 21))

enum { kAdsrSampleAddr = 0x1080 };

// A 2-block looping ADPCM payload so voice 1 never stops while the envelope
// runs (block 1 loops back to block 0). The decoded samples are irrelevant;
// the tests only read the envelope level.
static void adsr_upload_loop_sample(void) {
    unsigned char payload[32];
    int i;
    memset(payload, 0, sizeof(payload));
    payload[1] = 0x04;  // block 0: loop-start
    payload[17] = 0x03; // block 1: loop-end + repeat
    for (i = 0; i < 14; i++)
        payload[18 + i] = 0x55;
    Psyz_SpuMemWrite(kAdsrSampleAddr, payload, sizeof(payload));
}

static unsigned short adsr_status(void) { return Psyz_SpuRead(0x1AE); }
static unsigned short adsr_envx1(void) { return Psyz_SpuRead(0x1C); }

static void adsr_tick(void) {
    short frame[2];
    Psyz_SpuPullSamples(frame, 1);
}

static void adsr_wait_bit11_flip(void) {
    int guard;
    for (guard = 0; !(adsr_status() & 0x0800); guard++) {
        adsr_tick();
        if (guard >= 4096) {
            zfail("bit11 never went high (guard %d)", guard);
            return;
        }
    }
    for (guard = 0; adsr_status() & 0x0800; guard++) {
        adsr_tick();
        if (guard >= 4096) {
            zfail("bit11 never went low (guard %d)", guard);
            return;
        }
    }
}

// Capture n_samples ENVX readings for `adsr`, one per bit-11 flip.
static void adsr_capture(uint32_t adsr, uint16_t* envx, unsigned n_samples) {
    int guard;
    unsigned i;
    // Drain any previous envelope back to zero.
    spu_reset_quiet();
    Psyz_SpuWrite(0x1AA, 0x8000 | 0x4000);
    Psyz_SpuWrite(0x180, 0);
    Psyz_SpuWrite(0x182, 0);
    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);
    adsr_wait_bit11_flip();
    guard = 0;
    while (adsr_envx1() != 0) {
        adsr_tick();
        if (++guard >= 200000) {
            zfail(
                "previous envelope never went back to zero (guard %d)", guard);
            return;
        }
    }

    // Prepare voice 1, wait for a flip, then fire it.
    adsr_upload_loop_sample();
    Psyz_SpuWrite(0x14, 0x1000);                                 // sampleRate
    Psyz_SpuWrite(0x16, (unsigned short)(kAdsrSampleAddr >> 3)); // startAddr
    Psyz_SpuWrite(0x1E, (unsigned short)(kAdsrSampleAddr >> 3)); // repeatAddr
    Psyz_SpuWrite(0x10, 0);                                      // volumeLeft
    Psyz_SpuWrite(0x12, 0);                                      // volumeRight
    adsr_wait_bit11_flip();
    Psyz_SpuWrite(0x18, (unsigned short)(adsr & 0xFFFF));
    Psyz_SpuWrite(0x1A, (unsigned short)(adsr >> 16));
    Psyz_SpuWrite(0x18C, 0);
    Psyz_SpuWrite(0x18E, 0);
    Psyz_SpuWrite(0x188, 1u << 1); // KEY_ON voice 1

    for (guard = 0; adsr_envx1() == 0; guard++) {
        adsr_tick();
        if (guard >= 100000) {
            zfail("envelope never started after key-on (guard %d)", guard);
            return;
        }
    }
    envx[0] = adsr_envx1();
    for (i = 1; i < n_samples; i++) {
        adsr_wait_bit11_flip();
        envx[i] = adsr_envx1();
    }
    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);
}

static void adsr_capture_with_keyoff(
    uint32_t adsr, uint16_t* envx, unsigned n_samples, unsigned keyoff_at) {
    unsigned i;
    adsr_capture(adsr, envx, keyoff_at + 1);
    for (i = keyoff_at + 1; i < n_samples; i++) {
        adsr_wait_bit11_flip();
        envx[i] = adsr_envx1();
    }
}

#define EXPECT_ENVX_NEAR(nominal, step, got)                                   \
    do {                                                                       \
        int ok_ = (got) >= (uint16_t)((nominal) - (step)) &&                   \
                  (got) <= (uint16_t)((nominal) + (step));                     \
        if (!zexpect_s32_ne(0, ok_)) {                                         \
            zprintf("envx 0x%x not within %d of 0x%x\n", (unsigned)(got),      \
                    (int)(step), (unsigned)(nominal));                         \
        }                                                                      \
    } while (0)

ZTEST(spu, adsr_attack_linear_step) {
    uint16_t envx[0x40];
    const uint32_t base =
        ADSR_DECAY(0) | ADSR_SUSTAIN(3, 0x1f, 15, 0, 0) | ADSR_RELEASE(0, 0);
    int i;

    adsr_capture(ADSR_ATTACK(2, 12, 0) | base, envx, 4);
    zexpect_u16_eq(0x0005, envx[0]);
    EXPECT_ENVX_NEAR(0x04f1, 5, envx[1]);
    EXPECT_ENVX_NEAR(0x09f1, 5, envx[2]);
    EXPECT_ENVX_NEAR(0x0ef1, 5, envx[3]);

    adsr_capture(ADSR_ATTACK(3, 12, 0) | base, envx, 4);
    zexpect_u16_eq(0x0004, envx[0]);
    EXPECT_ENVX_NEAR(0x03f4, 4, envx[1]);
    EXPECT_ENVX_NEAR(0x07f4, 4, envx[2]);
    EXPECT_ENVX_NEAR(0x0bf4, 4, envx[3]);

    adsr_capture(ADSR_ATTACK(2, 24, 0) | base, envx, 48);
    for (i = 0; i < 17; i++)
        zexpect_u16_eq(0x0005, envx[i]);
    for (i = 17; i < 33; i++)
        zexpect_u16_eq(0x000a, envx[i]);
    for (i = 33; i < 48; i++)
        zexpect_u16_eq(0x000f, envx[i]);

    adsr_capture(ADSR_ATTACK(3, 24, 0) | base, envx, 48);
    for (i = 0; i < 17; i++)
        zexpect_u16_eq(0x0004, envx[i]);
    for (i = 17; i < 33; i++)
        zexpect_u16_eq(0x0008, envx[i]);
    for (i = 33; i < 48; i++)
        zexpect_u16_eq(0x000c, envx[i]);
}

ZTEST(spu, adsr_attack_linear_shift) {
    uint16_t envx[0x40];
    const uint32_t base =
        ADSR_DECAY(0) | ADSR_SUSTAIN(3, 0x1f, 15, 0, 0) | ADSR_RELEASE(0, 0);
    static const uint16_t s012[] = {
        0x06e4, 0x0de4, 0x14e4, 0x1be4, 0x22e4, 0x29e4, 0x30e4, 0x37e4, 0x3ee4,
        0x45e4, 0x4ce4, 0x53e4, 0x5ae4, 0x61e4, 0x68e4, 0x6fe4, 0x76e4, 0x7de4};
    int i;

    adsr_capture(ADSR_ATTACK(0, 0, 0) | base, envx, 1);
    zexpect_u16_eq(0x3800, envx[0]);
    adsr_capture(ADSR_ATTACK(1, 0, 0) | base, envx, 1);
    zexpect_u16_eq(0x3000, envx[0]);

    adsr_capture(ADSR_ATTACK(0, 11, 0) | base, envx, 4);
    zexpect_u16_eq(0x0007, envx[0]);
    EXPECT_ENVX_NEAR(0x0ddd, 7, envx[1]);
    EXPECT_ENVX_NEAR(0x1bdd, 7, envx[2]);
    EXPECT_ENVX_NEAR(0x29dd, 7, envx[3]);

    adsr_capture(ADSR_ATTACK(1, 11, 0) | base, envx, 4);
    zexpect_u16_eq(0x0006, envx[0]);
    EXPECT_ENVX_NEAR(0x0be2, 7, envx[1]);
    EXPECT_ENVX_NEAR(0x17e2, 7, envx[2]);
    EXPECT_ENVX_NEAR(0x23e2, 7, envx[3]);

    adsr_capture(ADSR_ATTACK(0, 12, 0) | base, envx, 32);
    zexpect_u16_eq(0x0007, envx[0]);
    for (i = 0; i < 18; i++)
        EXPECT_ENVX_NEAR(s012[i], 7, envx[i + 1]);

    adsr_capture(ADSR_ATTACK(1, 12, 0) | base, envx, 4);
    zexpect_u16_eq(0x0006, envx[0]);
    EXPECT_ENVX_NEAR(0x05ee, 6, envx[1]);
    EXPECT_ENVX_NEAR(0x0bee, 6, envx[2]);
    EXPECT_ENVX_NEAR(0x11ee, 6, envx[3]);

    adsr_capture(ADSR_ATTACK(0, 23, 0) | base, envx, 48);
    for (i = 0; i < 9; i++)
        zexpect_u16_eq(0x0007, envx[i]);
    for (i = 9; i < 17; i++)
        zexpect_u16_eq(0x000e, envx[i]);
    for (i = 17; i < 25; i++)
        zexpect_u16_eq(0x0015, envx[i]);
    for (i = 25; i < 33; i++)
        zexpect_u16_eq(0x001c, envx[i]);
    for (i = 33; i < 41; i++)
        zexpect_u16_eq(0x0023, envx[i]);
    for (i = 41; i < 48; i++)
        zexpect_u16_eq(0x002a, envx[i]);

    adsr_capture(ADSR_ATTACK(1, 23, 0) | base, envx, 48);
    for (i = 0; i < 9; i++)
        zexpect_u16_eq(0x0006, envx[i]);
    for (i = 9; i < 17; i++)
        zexpect_u16_eq(0x000c, envx[i]);
    for (i = 17; i < 25; i++)
        zexpect_u16_eq(0x0012, envx[i]);
    for (i = 25; i < 33; i++)
        zexpect_u16_eq(0x0018, envx[i]);
    for (i = 33; i < 41; i++)
        zexpect_u16_eq(0x001e, envx[i]);
    for (i = 41; i < 48; i++)
        zexpect_u16_eq(0x0024, envx[i]);

    adsr_capture(ADSR_ATTACK(0, 24, 0) | base, envx, 48);
    for (i = 0; i < 17; i++)
        zexpect_u16_eq(0x0007, envx[i]);
    for (i = 17; i < 33; i++)
        zexpect_u16_eq(0x000e, envx[i]);
    for (i = 33; i < 48; i++)
        zexpect_u16_eq(0x0015, envx[i]);

    adsr_capture(ADSR_ATTACK(1, 24, 0) | base, envx, 48);
    for (i = 0; i < 17; i++)
        zexpect_u16_eq(0x0006, envx[i]);
    for (i = 17; i < 33; i++)
        zexpect_u16_eq(0x000c, envx[i]);
    for (i = 33; i < 48; i++)
        zexpect_u16_eq(0x0012, envx[i]);
}

ZTEST(spu, adsr_attack_exponential) {
    uint16_t envx[0x40];
    const uint32_t base =
        ADSR_DECAY(0) | ADSR_SUSTAIN(3, 0x1f, 15, 0, 0) | ADSR_RELEASE(0, 0);
    static const uint16_t e[] = {
        0x06e4, 0x0de4, 0x14e4, 0x1be4, 0x22e4, 0x29e4, 0x30e4, 0x37e4,
        0x3ee4, 0x45e4, 0x4ce4, 0x53e4, 0x5ae4, 0x607f, 0x623f, 0x63ff,
        0x65bf, 0x677f, 0x693f, 0x6aff, 0x6cbf, 0x6e7f, 0x703f, 0x71ff,
        0x73bf, 0x757f, 0x773f, 0x78ff, 0x7abf, 0x7c7f, 0x7e3f};
    int i;

    adsr_capture(ADSR_ATTACK(0, 12, 1) | base, envx, 32);
    zexpect_u16_eq(0x0007, envx[0]);
    for (i = 0; i < 31; i++)
        EXPECT_ENVX_NEAR(e[i], 7, envx[i + 1]);
}

// Nominal ENVX checkpoints use a synchronized independent SPU replay
// (Beetle PSX 9164674). Keep the original one-step capture tolerances;
// CPU polling latency on hardware can shift a bit-11 checkpoint slightly.
ZTEST(spu, adsr_decay_shift) {
    uint16_t envx[0x40];
    int i;

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(10) |
                     ADSR_SUSTAIN(0, 0, 0, 1, 0) | ADSR_RELEASE(0, 1),
                 envx, 16);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x635d, 0x07, envx[1]);
    EXPECT_ENVX_NEAR(0x4c86, 0x05, envx[2]);
    EXPECT_ENVX_NEAR(0x3aba, 0x04, envx[3]);
    EXPECT_ENVX_NEAR(0x2ce6, 0x03, envx[4]);
    EXPECT_ENVX_NEAR(0x2215, 0x03, envx[5]);
    EXPECT_ENVX_NEAR(0x19aa, 0x02, envx[6]);
    EXPECT_ENVX_NEAR(0x133f, 0x02, envx[7]);
    EXPECT_ENVX_NEAR(0x0e2a, 0x01, envx[8]);
    EXPECT_ENVX_NEAR(0x0a2a, 0x01, envx[9]);
    for (i = 10; i < 16; i++)
        zexpect_u16_eq(0x0000, envx[i]);

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(14) |
                     ADSR_SUSTAIN(0, 0, 0, 1, 0) | ADSR_RELEASE(0, 1),
                 envx, 32);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x7e0f, 0x02, envx[1]);
    EXPECT_ENVX_NEAR(0x7c0f, 0x02, envx[2]);
    EXPECT_ENVX_NEAR(0x780f, 0x02, envx[4]);
    EXPECT_ENVX_NEAR(0x740f, 0x02, envx[6]);
    EXPECT_ENVX_NEAR(0x700f, 0x02, envx[8]);
    EXPECT_ENVX_NEAR(0x690d, 0x02, envx[12]);
    EXPECT_ENVX_NEAR(0x620d, 0x02, envx[16]);
    EXPECT_ENVX_NEAR(0x5bc2, 0x02, envx[20]);
    EXPECT_ENVX_NEAR(0x55c2, 0x02, envx[24]);
    EXPECT_ENVX_NEAR(0x4fcc, 0x02, envx[28]);
    EXPECT_ENVX_NEAR(0x4c0c, 0x02, envx[31]);
}

typedef struct {
    int level;
    uint16_t v;
    int start;
} AdsrSustainCase;

ZTEST(spu, adsr_sustain_level) {
    uint16_t envx[16];
    static const AdsrSustainCase cases[] = {
        {0, 0x07ff, 10},
        {7, 0x3ffa, 3},
        {11, 0x5ff6, 2},
        {14, 0x77ff, 1},
        {15, 0x7fef, 1}};
    size_t n;
    int i;
    for (n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
        const AdsrSustainCase* c = &cases[n];
        adsr_capture(
            ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(10) |
                ADSR_SUSTAIN(3, 0x1f, c->level, 1, 0) | ADSR_RELEASE(0, 1),
            envx, 16);
        for (i = c->start; i < 16; i++) {
            if (!zexpect_u16_eq(c->v, envx[i])) {
                zprintf("level %d sample %d\n", c->level, i);
            }
        }
    }
}

ZTEST(spu, adsr_sustain_up_linear) {
    uint16_t envx[0x20];
    int i;

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(0) |
                     ADSR_SUSTAIN(0, 10, 15, 0, 0) | ADSR_RELEASE(0, 1),
                 envx, 8);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x5b65, 0x07, envx[1]);
    EXPECT_ENVX_NEAR(0x7765, 0x07, envx[2]);
    for (i = 3; i < 8; i++)
        zexpect_u16_eq(0x7fff, envx[i]);

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(0) |
                     ADSR_SUSTAIN(0, 14, 15, 0, 0) | ADSR_RELEASE(0, 1),
                 envx, 32);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x41b1, 0x02, envx[1]);
    EXPECT_ENVX_NEAR(0x46f1, 0x02, envx[4]);
    EXPECT_ENVX_NEAR(0x4df1, 0x02, envx[8]);
    EXPECT_ENVX_NEAR(0x54f1, 0x02, envx[12]);
    EXPECT_ENVX_NEAR(0x5bf1, 0x02, envx[16]);
    EXPECT_ENVX_NEAR(0x70f1, 0x02, envx[28]);
    EXPECT_ENVX_NEAR(0x7631, 0x02, envx[31]);
}

ZTEST(spu, adsr_sustain_down_linear) {
    uint16_t envx[0x20];
    int i;

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(0) |
                     ADSR_SUSTAIN(3, 10, 15, 1, 0) | ADSR_RELEASE(0, 1),
                 envx, 8);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x2c6d, 0x05, envx[1]);
    EXPECT_ENVX_NEAR(0x186d, 0x05, envx[2]);
    EXPECT_ENVX_NEAR(0x046d, 0x05, envx[3]);
    for (i = 4; i < 8; i++)
        zexpect_u16_eq(0x0000, envx[i]);

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(0) |
                     ADSR_SUSTAIN(0, 14, 15, 1, 0) | ADSR_RELEASE(0, 1),
                 envx, 32);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x3e0f, 0x02, envx[1]);
    EXPECT_ENVX_NEAR(0x380f, 0x02, envx[4]);
    EXPECT_ENVX_NEAR(0x300f, 0x02, envx[8]);
    EXPECT_ENVX_NEAR(0x200f, 0x02, envx[16]);
    EXPECT_ENVX_NEAR(0x100f, 0x02, envx[24]);
    EXPECT_ENVX_NEAR(0x020f, 0x02, envx[31]);
}

ZTEST(spu, adsr_sustain_up_exponential) {
    uint16_t envx[0x20];

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(0) |
                     ADSR_SUSTAIN(0, 12, 15, 0, 1) | ADSR_RELEASE(0, 1),
                 envx, 24);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x46d5, 0x04, envx[1]);
    EXPECT_ENVX_NEAR(0x5bd5, 0x04, envx[4]);
    EXPECT_ENVX_NEAR(0x60b3, 0x02, envx[5]);
    EXPECT_ENVX_NEAR(0x67b3, 0x02, envx[9]);
    EXPECT_ENVX_NEAR(0x73f3, 0x02, envx[16]);
    EXPECT_ENVX_NEAR(0x7af3, 0x02, envx[20]);
    zexpect_u16_eq(0x7fff, envx[23]);
}

ZTEST(spu, adsr_sustain_down_exponential) {
    uint16_t envx[0x20];

    adsr_capture(ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(0) |
                     ADSR_SUSTAIN(0, 12, 15, 1, 1) | ADSR_RELEASE(0, 1),
                 envx, 32);
    zexpect_u16_eq(0x1c00, envx[0]);
    EXPECT_ENVX_NEAR(0x3c19, 0x02, envx[1]);
    EXPECT_ENVX_NEAR(0x3019, 0x02, envx[4]);
    EXPECT_ENVX_NEAR(0x2112, 0x02, envx[9]);
    EXPECT_ENVX_NEAR(0x1eb7, 0x01, envx[10]);
    EXPECT_ENVX_NEAR(0x12b7, 0x01, envx[16]);
    EXPECT_ENVX_NEAR(0x095b, 0x01, envx[24]);
    EXPECT_ENVX_NEAR(0x035b, 0x01, envx[30]);
}

ZTEST(spu, adsr_release_linear) {
    uint16_t envx[0x20];
    int i;

    adsr_capture_with_keyoff(
        ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(15) |
            ADSR_SUSTAIN(3, 0x1f, 15, 0, 0) | ADSR_RELEASE(12, 0),
        envx, 24, 3);
    zexpect_u16_eq(0x1c00, envx[0]);
    zexpect_u16_eq(0x7ff7, envx[1]);
    zexpect_u16_eq(0x7ff7, envx[2]);
    zexpect_u16_eq(0x7ff7, envx[3]);
    EXPECT_ENVX_NEAR(0x77fb, 0x04, envx[4]);
    EXPECT_ENVX_NEAR(0x47fb, 0x04, envx[10]);
    EXPECT_ENVX_NEAR(0x1ffb, 0x04, envx[15]);
    EXPECT_ENVX_NEAR(0x07fb, 0x04, envx[18]);
    for (i = 19; i < 24; i++)
        zexpect_u16_eq(0x0000, envx[i]);
}

ZTEST(spu, adsr_release_exponential) {
    uint16_t envx[0x20];

    adsr_capture_with_keyoff(
        ADSR_ATTACK(0, 1, 0) | ADSR_DECAY(15) |
            ADSR_SUSTAIN(3, 0x1f, 15, 0, 0) | ADSR_RELEASE(12, 1),
        envx, 24, 2);
    zexpect_u16_eq(0x1c00, envx[0]);
    zexpect_u16_eq(0x7ff7, envx[1]);
    zexpect_u16_eq(0x7ff7, envx[2]);
    EXPECT_ENVX_NEAR(0x77fb, 0x04, envx[3]);
    EXPECT_ENVX_NEAR(0x61fb, 0x04, envx[6]);
    EXPECT_ENVX_NEAR(0x45bf, 0x03, envx[11]);
    EXPECT_ENVX_NEAR(0x3099, 0x02, envx[16]);
    EXPECT_ENVX_NEAR(0x2172, 0x02, envx[21]);
    EXPECT_ENVX_NEAR(0x1cf7, 0x01, envx[23]);
}

enum {
    kRoomWorkAddr = 0x7D940,
    kRoomWorkBytes = 0x26C0,
    kRoomLapFrames = kRoomWorkBytes,
};

static void reverb_room_setup(bool master_enable, bool voice1_reverb) {
    SpuReverbAttr attr;
    spu_reset_quiet();
    SpuInit();
    Psyz_SpuWrite(0x1AA, 0x8000 | 0x4000);
    Psyz_SpuWrite(0x180, 0x3FFF);
    Psyz_SpuWrite(0x182, 0x3FFF);

    memset(&attr, 0, sizeof(attr));
    attr.mask = SPU_REV_MODE;
    attr.mode = SPU_REV_MODE_ROOM | SPU_REV_MODE_CLEAR_WA;
    if (!zexpect_s32_eq(0, SpuSetReverbModeParam(&attr))) {
        return;
    }

    memset(&attr, 0, sizeof(attr));
    attr.mask = SPU_REV_DEPTHL | SPU_REV_DEPTHR;
    attr.depth.left = 0x3FFF;
    attr.depth.right = 0x3FFF;
    if (!zexpect_s32_eq(0, SpuSetReverbModeParam(&attr))) {
        return;
    }

    if (master_enable) {
        if (!zexpect_s32_eq(SPU_ON, SpuSetReverb(SPU_ON))) {
            return;
        }
    }
    Psyz_SpuWrite(0x198, voice1_reverb ? (1u << 1) : 0);

    Psyz_SpuWrite((1 << 4) + 0x00, 0x3FFF);
    Psyz_SpuWrite((1 << 4) + 0x02, 0x3FFF);
}

static void run_reverb_room(
    const unsigned char* sample64, int nframes, unsigned char* work_out) {
    unsigned char upload[128];
    memcpy(upload, sample64, 64);
    memset(upload + 64, 0xAA, 64);
    Psyz_SpuMemWrite(kSampleAddr, upload, sizeof(upload));

    spu_voice1_keyon(kSampleAddr, 0x1000);
    pull_samples_nop(nframes);
    Psyz_SpuMemRead(kRoomWorkAddr, work_out, kRoomWorkBytes);

    Psyz_SpuWrite(0x18C, 0xFFFF);
    Psyz_SpuWrite(0x18E, 0xFFFF);
}

static void dump_actual_bin(
    const char* name, const unsigned char* data, size_t size) {
    char path[256];
    FILE* f;
    snprintf(path, sizeof(path), "expected/spu/%s.actual.bin", name);
    f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

static int compare_work_area(const char* name, const unsigned char* got,
                             const unsigned char* want, size_t want_size) {
    unsigned i;
    if (want_size != kRoomWorkBytes) {
        dump_actual_bin(name, got, kRoomWorkBytes);
        zprintf("%s: golden is %zu bytes, want %u\n", name, want_size,
                (unsigned)kRoomWorkBytes);
        return 0;
    }
    for (i = 0; i < kRoomWorkBytes; i++) {
        if (got[i] != want[i]) {
            dump_actual_bin(name, got, kRoomWorkBytes);
            zprintf("%s: first mismatch at work area offset %u (SPU RAM "
                    "0x%05X): got 0x%02X, want 0x%02X\n",
                    name, i, kRoomWorkAddr + i, got[i], want[i]);
            return 0;
        }
    }
    return 1;
}

static int check_work_area(const char* name, const unsigned char* got) {
    size_t size;
    unsigned char* want =
        load_expected(name, ".test.bin", "expected file", &size);
    int ok = compare_work_area(name, got, want, size);
    free(want);
    return ok;
}

static bool all_zero(const unsigned char* p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}

// Work-RAM regression snapshot for the corrected Gaussian/pipeline/FIR and
// reverb fractional precision. This snapshot is generated by PsyZ, not captured
// from hardware. Independent hardware-data checks are GaussianHardwareImpulse
// and ReverbHardwareFirImpulse; GaussianKeyOnPitchPhase also compares an
// independently implemented SPU.
ZTEST(spu, reverb_room_work_area) {
    zskip_targets("psp"); // can't capture reverb on PSP SAS
    unsigned char work[kRoomWorkBytes] = {0};
    reverb_room_setup(true, true);
    run_reverb_room(kAdpcmSine, kRoomLapFrames, work);

    if (!zexpect_s32_eq(0, all_zero(work, sizeof(work)))) {
        zprintf("reverb wrote nothing to its work area\n");
    }
    zexpect_s32_eq(1, check_work_area("reverb_room", work));
}

ZTEST(spu, ReverbHardwareFirImpulse) {
    // With all-pass gains zero and writes disabled, APF2 reads the impulse
    // directly from work RAM. The right-channel upsampler must reproduce
    // the published 39-tap half-band response, including its 19-tick delay.
    static const short coefficients[39] = {
        -1,   0, 2,     0, -10,   0,     35,    0, -103,  0, 266,  0, -616, 0,
        1332, 0, -2960, 0, 10246, 16384, 10246, 0, -2960, 0, 1332, 0, -616, 0,
        266,  0, -103,  0, 35,    0,     -10,   0, 2,     0, -1};
    short impulse = 16384, pcm[128];
    Psyz_SpuReset(0);
    for (unsigned offset = 0x1C0; offset < 0x200; offset += 2)
        Psyz_SpuWrite(offset, 0);
    Psyz_SpuMemWrite(0x70000, &impulse, 2);
    Psyz_SpuWrite(0x1A2, 0x70000 >> 3);
    Psyz_SpuWrite(
        0x1AA, 0xC000); // reverb still reads; work RAM writes disabled
    Psyz_SpuWrite(0x180, 0x3FFF);
    Psyz_SpuWrite(0x182, 0x3FFF);
    Psyz_SpuWrite(0x184, 0);
    Psyz_SpuWrite(0x186, 0x7FFF);
    Psyz_SpuPullSamples(pcm, 64);
    for (unsigned frame = 0; frame < 64; ++frame) {
        int expected = frame > 0 && frame < 39 ? coefficients[frame]
                                               : 0; // initial gain latch
        expected = (expected * 32767) >> 15;
        expected = (expected * 32766) >> 15;
        zexpect_s32_eq(0, pcm[frame * 2]);
        zexpect_s32_eq(expected, pcm[frame * 2 + 1]);
    }
    short unchanged = 0;
    Psyz_SpuMemRead(0x70000, &unchanged, 2);
    zexpect_s32_eq(impulse, unchanged);
}

ZTEST(spu, reverb_master_disabled_leaves_work_area_clear) {
    unsigned char work[kRoomWorkBytes] = {0};
    reverb_room_setup(false, true);
    run_reverb_room(kAdpcmSine, kRoomLapFrames, work);

    if (!zexpect_s32_ne(0, all_zero(work, sizeof(work)))) {
        zprintf("reverb wrote to its work area with SPUCNT bit 7 clear\n");
    }
}

ZTEST(spu, reverb_unrouted_voice_leaves_work_area_clear) {
    unsigned char work[kRoomWorkBytes] = {0};
    reverb_room_setup(true, false);
    run_reverb_room(kAdpcmSine, kRoomLapFrames, work);

    if (!zexpect_s32_ne(0, all_zero(work, sizeof(work)))) {
        zprintf(
            "reverb work area is non-zero with no voice routed to reverb\n");
    }
}

ZTEST(spu, SetReverbModeParamLoadsWholePreset) {
    static const u16 dapf1[SPU_REV_MODE_MAX] = {
        0x0000, 0x007D, 0x0033, 0x00B1, 0x00E3,
        0x01A5, 0x033D, 0x0001, 0x0001, 0x0017};
    static const u16 viir[SPU_REV_MODE_MAX] = {
        0x0000, 0x6D80, 0x70F0, 0x70F0, 0x6F60,
        0x6000, 0x7E00, 0x7FFF, 0x7FFF, 0x70F0};
    SpuReverbAttr attr;
    int mode;
    spu_reset_quiet();
    SpuInit();
    for (mode = SPU_REV_MODE_OFF; mode < SPU_REV_MODE_MAX; mode++) {
        memset(&attr, 0, sizeof(attr));
        attr.mask = SPU_REV_MODE;
        attr.mode = mode;
        zassert_s32_eq(0, SpuSetReverbModeParam(&attr));
        zexpect_u16_eq(dapf1[mode], Psyz_SpuRead(0x1C0));
        zexpect_u16_eq(viir[mode], Psyz_SpuRead(0x1C4));
        zexpect_u16_eq(mode ? 0x8000 : 0, Psyz_SpuRead(0x1FC));
        zexpect_u16_eq(mode ? 0x8000 : 0, Psyz_SpuRead(0x1FE));
    }
}

#ifndef LIBSPU_INTERNAL_H
typedef struct tagSpuMalloc {
    u32 addr;
    u32 size;
} SPU_MALLOC;
#endif
extern SPU_MALLOC* _spu_memList;
extern int _spu_mem_mode_plus;
extern int _spu_rev_reserve_wa;
extern int _spu_rev_offsetaddr;
extern int _SpuIsInAllocateArea_(unsigned);
long SpuInitMalloc(long num, char* top);
long SpuMallocWithStartAddr(unsigned long addr, long size);

static char heap[0x1000];

ZTEST_SETUP(spu_malloc) {
    spu_setup();
    memset(heap, 0, sizeof(heap));
    SpuInit();
}

ZTEST(spu_malloc, SpuSetReverbOffClearsSpucnt) {
    Psyz_SpuWrite(0x1AA, 0x0080);

    zexpect_s32_eq(0, SpuSetReverb(SPU_OFF));
    zexpect_u16_eq(0x0000, Psyz_SpuRead(0x1AA));
}

ZTEST(spu_malloc, SpuSetReverbOnSetsSpucnt) {
    Psyz_SpuWrite(0x1AA, 0);
    SpuInitMalloc(32, heap);

    zexpect_s32_eq(3, _spu_mem_mode_plus);
    zexpect_s32_eq(0, _spu_rev_reserve_wa);
    zexpect_s32_eq(0xFFFE, _spu_rev_offsetaddr);
    zexpect_u32_eq(0x40001010u, _spu_memList[0].addr);
    zexpect_u32_eq(520176u, _spu_memList[0].size);
    zexpect_s32_eq(0, _SpuIsInAllocateArea_(_spu_rev_offsetaddr));

    zexpect_s32_eq(SPU_ON, SpuSetReverb(SPU_ON));
    zexpect_u16_eq(0x0080, Psyz_SpuRead(0x1AA));
}

ZTEST(spu_malloc, SpuMallocWithStartAddrSplitsFreeBlock) {
    SpuInitMalloc(32, heap);
    SpuMallocWithStartAddr(0x00001010, 0x00010000);

    zexpect_u32_eq(0x00001010u, _spu_memList[0].addr);
    zexpect_u32_eq(65536u, _spu_memList[0].size);
    zexpect_u32_eq(0x40011010u, _spu_memList[1].addr);
    zexpect_u32_eq(454640u, _spu_memList[1].size);
    zexpect_u32_eq(0u, _spu_memList[2].addr);
    zexpect_u32_eq(0u, _spu_memList[2].size);
}

#if defined(__psyz) && !defined(__PSP__)
#include <SDL3/SDL.h>
extern void Sdl3Audio_VBlank(bool pal);

typedef struct {
    short samples[4414 * 2];
    unsigned frames, calls;
    bool overflow;
} AudioCaptureProof;

static void capture_playback_pcm(
    const short* samples, int frames, void* userdata) {
    AudioCaptureProof* proof = userdata;
    ++proof->calls;
    if (frames <= 0 || (unsigned)frames > 4414 - proof->frames) {
        proof->overflow = true;
        return;
    }
    memcpy(proof->samples + proof->frames * 2, samples,
           (size_t)frames * 2 * sizeof(short));
    proof->frames += (unsigned)frames;
}

static void capture_test_voice(void) {
    spu_reset_quiet();
    Psyz_SpuMemWrite(kSampleAddr, kAdpcmSine, sizeof(kAdpcmSine));
    Psyz_SpuWrite(0x1AA, 0xC000);
    Psyz_SpuWrite(0x180, 0x3FFF);
    Psyz_SpuWrite(0x182, 0x3FFF);
    spu_voice1_keyon(kSampleAddr, 0x1000);
    Psyz_SpuWrite(0x10, 0x3FFF);
    Psyz_SpuWrite(0x12, 0x2000);
}
#endif

#if defined(__psyz) && !defined(__PSP__)
typedef struct {
    unsigned operation;
    SDL_Semaphore* entered;
    SDL_AtomicInt done;
} SpuConcurrentAccess;

static int SDLCALL access_spu_from_worker(void* userdata) {
    SpuConcurrentAccess* access = userdata;
    unsigned char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    short pcm[2];
    SDL_SignalSemaphore(access->entered);
    switch (access->operation) {
    case 0:
        Psyz_SpuInit();
        break;
    case 1:
        Psyz_SpuReset(0);
        break;
    case 2:
        Psyz_SpuReset(1);
        break;
    case 3:
        Psyz_SpuSetTransferAddr(0x1000);
        break;
    case 4:
        (void)Psyz_SpuGetTransferAddr();
        break;
    case 5:
        Psyz_SpuFifoWrite(0x1234);
        break;
    case 6:
        Psyz_SpuFifoWriteBulk(data, sizeof(data));
        break;
    case 7:
        Psyz_SpuMemWrite(0x200, data, sizeof(data));
        break;
    case 8:
        Psyz_SpuMemRead(0x200, data, sizeof(data));
        break;
    case 9:
        Psyz_SpuWrite(0x188, 1);
        break;
    case 10:
        (void)Psyz_SpuRead(0x1AA);
        break;
    case 11:
        Psyz_SpuPullSamples(pcm, 1);
        break;
    }
    SDL_SetAtomicInt(&access->done, 1);
    return 0;
}

static void count_concurrent_pcm(
    const short* samples, int frames, void* userdata) {
    (void)samples;
    SDL_AddAtomicInt((SDL_AtomicInt*)userdata, frames);
}
#endif

ZTEST(audio_sync, public_spu_access_waits_for_mixer_transaction) {
    zskip_targets("ps1;psp");
#if defined(__psyz) && !defined(__PSP__)
    const char* clock = SDL_getenv("PSYZ_AUDIO_CLOCK");
    char* saved = clock ? SDL_strdup(clock) : NULL;
    Psyz_AudioDestroy();
    SDL_SetEnvironmentVariable(
        SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK", "video", true);
    zassert_s32_eq(0, Psyz_AudioInit());
    unsigned early = 0, completed = 0, started = 0;
    for (unsigned operation = 0; operation < 12; ++operation) {
        SpuConcurrentAccess access = {.operation = operation};
        access.entered = SDL_CreateSemaphore(0);
        if (!access.entered)
            continue;
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(
            props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
            (void*)access_spu_from_worker);
        SDL_SetStringProperty(
            props, SDL_PROP_THREAD_CREATE_NAME_STRING, "SPU transaction probe");
        SDL_SetPointerProperty(
            props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, &access);
        /* Hot reset preserves 512 KiB on its caller's stack. */
        SDL_SetNumberProperty(
            props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 2 * 1024 * 1024);
        Psyz_AudioLock();
        SDL_Thread* thread = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
        if (thread) {
            if (SDL_WaitSemaphoreTimeout(access.entered, 2000)) {
                started |= 1u << operation;
                SDL_Delay(20);
                if (SDL_GetAtomicInt(&access.done))
                    early |= 1u << operation;
            }
        }
        Psyz_AudioUnlock();
        if (thread) {
            int status = -1;
            SDL_WaitThread(thread, &status);
            if (status == 0 && SDL_GetAtomicInt(&access.done))
                completed |= 1u << operation;
        }
        SDL_DestroySemaphore(access.entered);
    }
    Psyz_AudioDestroy();
    if (saved) {
        SDL_SetEnvironmentVariable(
            SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK", saved, true);
        SDL_free(saved);
    } else
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
    zassert_s32_eq(0, Psyz_AudioInit());
    zassert_u32_eq(0xFFF, started);
    zassert_u32_eq(0xFFF, completed);
    zassert_u32_eq(0, early);
#endif
}

ZTEST(audio_sync, device_mix_survives_concurrent_rekey_reset_and_ram_transfer) {
    zskip_targets("ps1;psp");
#if defined(__psyz) && !defined(__PSP__)
    const char* clock = SDL_getenv("PSYZ_AUDIO_CLOCK");
    char* saved = clock ? SDL_strdup(clock) : NULL;
    Psyz_AudioDestroy();
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
    zassert_s32_eq(0, Psyz_AudioInit());
    SDL_AtomicInt rendered = {0};
    capture_test_voice();
    Psyz_AudioSetCaptureCallback(count_concurrent_pcm, &rendered);
    for (unsigned i = 0; i < 20000; ++i) {
        if ((i & 1023) == 0)
            Psyz_SpuReset(1);
        Psyz_SpuWrite(0x04, (unsigned short)(1 + (i * 97) % 0x3FFF));
        if ((i & 15) == 0)
            Psyz_SpuMemWrite(kSampleAddr, kAdpcmSine, sizeof(kAdpcmSine));
        Psyz_SpuWrite(0x188, 1);
        if ((i & 7) == 0)
            Psyz_SpuWrite(0x18C, 1);
        if ((i & 63) == 0)
            SDL_Delay(1);
    }
    for (unsigned wait = 0; wait < 200 && !SDL_GetAtomicInt(&rendered); ++wait)
        SDL_Delay(5);
    Psyz_AudioSetCaptureCallback(NULL, NULL);
    int frames = SDL_GetAtomicInt(&rendered);
    Psyz_AudioDestroy();
    if (saved) {
        SDL_SetEnvironmentVariable(
            SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK", saved, true);
        SDL_free(saved);
    } else
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
    zassert_s32_eq(0, Psyz_AudioInit());
    zassert_s32_eq(1, frames > 0);
#endif
}

ZTEST(audio_capture, video_clock_observes_existing_pcm_without_advancing_spu) {
    zskip_targets("ps1;psp");
#if defined(__psyz) && !defined(__PSP__)
    const char* clock = SDL_getenv("PSYZ_AUDIO_CLOCK");
    char* saved = clock ? SDL_strdup(clock) : NULL;
    Psyz_AudioDestroy();
    zassert_s32_eq(
        1, SDL_SetEnvironmentVariable(
               SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK", "video", true));
    zassert_s32_eq(0, Psyz_AudioInit());
    static short expected[4414 * 2], expected_tail[96 * 2], tail[96 * 2];
    static AudioCaptureProof proof;
    memset(&proof, 0, sizeof(proof));
    capture_test_voice();
    Psyz_SpuPullSamples(expected, 4414);
    Psyz_SpuPullSamples(expected_tail, 96);
    unsigned nonzero = 0;
    for (unsigned i = 0; i < 4414 * 2; ++i)
        if (expected[i])
            ++nonzero;
    zassert_s32_eq(1, nonzero > 0);
    capture_test_voice();
    Psyz_AudioSetCaptureCallback(capture_playback_pcm, &proof);
    Psyz_AudioPause();
    Sdl3Audio_VBlank(false);
    zassert_u32_eq(0, proof.calls);
    Psyz_AudioUnpause();
    for (unsigned i = 0; i < 6; ++i)
        Sdl3Audio_VBlank(false);
    zassert_u32_eq(6, proof.calls);
    zassert_u32_eq(4414, proof.frames);
    zassert_s32_eq(0, proof.overflow);
    zassert_s32_eq(0, memcmp(expected, proof.samples, sizeof(expected)));
    Psyz_AudioPause();
    Sdl3Audio_VBlank(false);
    zassert_u32_eq(6, proof.calls);
    Psyz_SpuPullSamples(tail, 96);
    zassert_s32_eq(0, memcmp(expected_tail, tail, sizeof(tail)));
    zassert_u32_eq(6, proof.calls);
    Psyz_AudioSetCaptureCallback(NULL, NULL);
    Psyz_AudioUnpause();
    Sdl3Audio_VBlank(false);
    zassert_u32_eq(6, proof.calls);
    Psyz_AudioSetCaptureCallback(capture_playback_pcm, &proof);
    Psyz_AudioDestroy();
    zassert_s32_eq(0, Psyz_AudioInit());
    Sdl3Audio_VBlank(false);
    zassert_u32_eq(6, proof.calls);
    Psyz_AudioDestroy();
    if (saved) {
        SDL_SetEnvironmentVariable(
            SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK", saved, true);
        SDL_free(saved);
    } else
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
    zassert_s32_eq(0, Psyz_AudioInit());
#endif
}

ZTEST(audio_capture, device_callback_observes_pcm_and_detaches) {
    zskip_targets("ps1;psp");
#if defined(__psyz) && !defined(__PSP__)
    const char* clock = SDL_getenv("PSYZ_AUDIO_CLOCK");
    char* saved = clock ? SDL_strdup(clock) : NULL;
    Psyz_AudioDestroy();
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
    zassert_s32_eq(0, Psyz_AudioInit());
    Psyz_AudioPause();
    Psyz_AudioLock();
    static short expected[4414 * 2];
    static AudioCaptureProof proof;
    memset(&proof, 0, sizeof(proof));
    capture_test_voice();
    Psyz_SpuPullSamples(expected, 4414);
    capture_test_voice();
    Psyz_AudioSetCaptureCallback(capture_playback_pcm, &proof);
    Psyz_AudioUnlock();
    Psyz_AudioUnpause();
    for (unsigned wait = 0; wait < 200; ++wait) {
        SDL_Delay(5);
        Psyz_AudioLock();
        unsigned frames = proof.frames;
        Psyz_AudioUnlock();
        if (frames)
            break;
    }
    Psyz_AudioSetCaptureCallback(NULL, NULL);
    zassert_s32_eq(1, proof.frames > 0);
    zassert_s32_eq(1, proof.frames <= 4414);
    zassert_s32_eq(0, memcmp(expected, proof.samples,
                             (size_t)proof.frames * 2 * sizeof(short)));
    unsigned calls = proof.calls;
    SDL_Delay(20);
    zassert_u32_eq(calls, proof.calls);
    Psyz_AudioDestroy();
    if (saved) {
        SDL_SetEnvironmentVariable(
            SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK", saved, true);
        SDL_free(saved);
    } else
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
    zassert_s32_eq(0, Psyz_AudioInit());
#endif
}

#if defined(__psyz) && !defined(__PSP__)
#include <kernel.h>
#include <libapi.h>
#include <libetc.h>
static unsigned offline_timer_ticks;
static bool offline_timer_mutes;
static long offline_timer_tick(void) {
    ++offline_timer_ticks;
    if (offline_timer_mutes) {
        unsigned volume = (offline_timer_ticks & 1) ? 0 : 0x3FFF;
        Psyz_SpuWrite(0x180, volume);
        Psyz_SpuWrite(0x182, volume);
    }
    return 1;
}
#endif

ZTEST(audio_clock, root_counter_tracks_samples_and_ignores_host_delay) {
    zskip_targets("ps1;psp");
#if defined(__psyz) && !defined(__PSP__)
    const char* clock = getenv("PSYZ_AUDIO_CLOCK");
    if (!clock || strcmp(clock, "video"))
        zskip("requires PSYZ_AUDIO_CLOCK=video at process startup");
    Psyz_AudioDestroy();
    zassert_s32_eq(0, Psyz_AudioInit());
    offline_timer_ticks = 0;
    zassert_s32_eq(1, SetRCnt(RCntCNT2, 0x44E8, RCntMdINTR));
    long event = OpenEvent(RCntCNT2, EvSpINT, EvMdINTR, offline_timer_tick);
    zassert_s32_eq(1, event != -1);
    zassert_s32_eq(1, EnableEvent(event));
    zassert_s32_eq(1, StartRCnt(RCntCNT2));
    for (unsigned i = 0; i < 6; ++i)
        Sdl3Audio_VBlank(false);
    zassert_u32_eq(24, offline_timer_ticks);
    long phase = GetRCnt(RCntCNT2);
    SDL_Delay(40);
    zassert_s32_eq(phase, GetRCnt(RCntCNT2));
    zassert_u32_eq(24, offline_timer_ticks);
    Psyz_AudioPause();
    Sdl3Audio_VBlank(false);
    zassert_u32_eq(24, offline_timer_ticks);
    Psyz_AudioUnpause();
    for (unsigned i = 0; i < 54; ++i)
        Sdl3Audio_VBlank(false);
    zassert_u32_eq(240, offline_timer_ticks);
    StopRCnt(RCntCNT2);
    DisableEvent(event);
    CloseEvent(event);
#endif
}

ZTEST(audio_clock, root_counter_changes_pcm_inside_video_frame) {
    zskip_targets("ps1;psp");
#if defined(__psyz) && !defined(__PSP__)
    const char* clock = getenv("PSYZ_AUDIO_CLOCK");
    if (!clock || strcmp(clock, "video"))
        zskip("requires PSYZ_AUDIO_CLOCK=video at process startup");
    Psyz_AudioDestroy();
    zassert_s32_eq(0, Psyz_AudioInit());
    static short expected[4414 * 2];
    static AudioCaptureProof proof;
    memset(&proof, 0, sizeof(proof));
    capture_test_voice();
    Psyz_SpuPullSamples(expected, 4414);
    capture_test_voice();
    offline_timer_ticks = 0;
    offline_timer_mutes = true;
    zassert_s32_eq(1, SetRCnt(RCntCNT2, 0x44E8, RCntMdINTR));
    long event = OpenEvent(RCntCNT2, EvSpINT, EvMdINTR, offline_timer_tick);
    zassert_s32_eq(1, event != -1);
    EnableEvent(event);
    StartRCnt(RCntCNT2);
    Psyz_AudioSetCaptureCallback(capture_playback_pcm, &proof);
    for (unsigned i = 0; i < 6; ++i)
        Sdl3Audio_VBlank(false);
    Psyz_AudioSetCaptureCallback(NULL, NULL);
    StopRCnt(RCntCNT2);
    DisableEvent(event);
    CloseEvent(event);
    offline_timer_mutes = false;
    zassert_u32_eq(24, offline_timer_ticks);
    zassert_u32_eq(4414, proof.frames);
    zassert_s32_eq(0, proof.overflow);
    for (unsigned frame = 0; frame < 4414; ++frame) {
        bool muted = ((frame * 4 / 735) & 1) != 0;
        for (unsigned ch = 0; ch < 2; ++ch)
            zassert_s32_eq(muted ? 0 : expected[frame * 2 + ch],
                           proof.samples[frame * 2 + ch]);
    }
#endif
}

ZTEST(audio_clock, synchronous_wait_services_timer_without_vblank) {
    zskip_targets("ps1;psp");
#if defined(__psyz) && !defined(__PSP__)
    const char* clock = getenv("PSYZ_AUDIO_CLOCK");
    if (!clock || strcmp(clock, "video"))
        zskip("requires PSYZ_AUDIO_CLOCK=video at process startup");
    Psyz_AudioDestroy();
    zassert_s32_eq(0, Psyz_AudioInit());
    capture_test_voice();
    static AudioCaptureProof proof;
    memset(&proof, 0, sizeof(proof));
    offline_timer_ticks = 0;
    zassert_s32_eq(1, SetRCnt(RCntCNT2, 0x44E8, RCntMdINTR));
    long event = OpenEvent(RCntCNT2, EvSpINT, EvMdINTR, offline_timer_tick);
    zassert_s32_eq(1, event != -1);
    EnableEvent(event);
    StartRCnt(RCntCNT2);
    Psyz_AudioSetCaptureCallback(capture_playback_pcm, &proof);
    int vblank = VSync(-1);
    for (unsigned i = 1; i <= 4; ++i) {
        zassert_s32_eq(1, Psyz_AudioWaitForTimer());
        zassert_u32_eq(i, offline_timer_ticks);
    }
    zassert_s32_eq(vblank, VSync(-1));
    zassert_u32_eq(735, proof.frames);
    zassert_u32_eq(4, proof.calls);
    zassert_s32_eq(0, proof.overflow);
    Psyz_AudioPause();
    zassert_s32_eq(0, Psyz_AudioWaitForTimer());
    zassert_u32_eq(4, proof.calls);
    Psyz_AudioSetCaptureCallback(NULL, NULL);
    Psyz_AudioUnpause();
    StopRCnt(RCntCNT2);
    DisableEvent(event);
    CloseEvent(event);
#endif
}
