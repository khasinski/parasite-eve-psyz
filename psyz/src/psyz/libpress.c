#include <psyz.h>
#include <libpress.h>
#include <psyz/log.h>
#include <stdlib.h>
#include <string.h>
#include "../internal.h"
#include "../../../decomp/src/libpress/libpress_private.h"

/* Native implementation of the MDEC commands used by LIBPRESS. Serialized
 * words are always 32 bits, even where the SDK's u_long is a native pointer. */
static struct {
    DECDCTENV env;
    u8* output;
    size_t capacity, length, cursor;
    u32 command;
    int error;
} mdec;

static const u8 scan[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static int floor_shift(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    return (int)(value >= 0 ? value / divisor
                            : -((-value + divisor - 1) / divisor));
}
static int clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}
static int sign_bits(unsigned value, unsigned bits) {
    unsigned sign = 1u << (bits - 1);
    return (int)(value & (sign - 1)) - (int)(value & sign);
}
static u16 read16(const u8* p) { return p[0] | (u16)p[1] << 8; }
static u32 read32(const u8* p) { return read16(p) | (u32)read16(p + 2) << 16; }

static void inverse_dct(int block[64]) {
    int work[64];
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                int64_t sum = 0;
                for (unsigned z = 0; z < 8; ++z)
                    sum += (int64_t)block[y + z * 8] *
                           floor_shift(mdec.env.dct[x + z * 8], 3);
                work[x + y * 8] = floor_shift(sum + 0xfff, 13);
            }
        memcpy(block, work, sizeof(work));
    }
    for (unsigned i = 0; i < 64; ++i)
        block[i] = sign_bits((unsigned)block[i], 9);
}

/* 1 = block, 0 = padding/end of input, -1 = incomplete block. */
static int decode_block(
    const u8** source, const u8* end, int block[64], const u8 quant[64]) {
    const u8* p = *source;
    unsigned code;
    do {
        if (p == end) {
            *source = p;
            return 0;
        }
        code = read16(p);
        p += 2;
    } while (code == 0xfe00);
    memset(block, 0, 64 * sizeof(*block));
    unsigned scale = code >> 10, index = 0;
    int value = sign_bits(code, 10) * quant[0];
    for (;;) {
        if (!scale)
            value = sign_bits(code, 10) * 2;
        block[scale ? scan[index] : index] = clamp(value, -1024, 1023);
        if (index == 63)
            break;
        if (p == end) {
            *source = p;
            return -1;
        }
        code = read16(p);
        p += 2;
        index += (code >> 10) + 1;
        if (index >= 64)
            break;
        value = floor_shift(
            (int64_t)sign_bits(code, 10) * quant[index] * scale + 4, 3);
    }
    inverse_dct(block);
    *source = p;
    return 1;
}

static int reserve(size_t bytes) {
    if (bytes <= mdec.capacity - mdec.length)
        return 1;
    size_t capacity = mdec.capacity ? mdec.capacity * 2 : 4096;
    if (capacity < mdec.length + bytes)
        capacity = mdec.length + bytes;
    void* allocation = realloc(mdec.output, capacity);
    if (!allocation)
        return 0;
    mdec.output = allocation;
    mdec.capacity = capacity;
    return 1;
}

static void decode(const u8* source, const u8* end) {
    unsigned depth = (mdec.command >> 27) & 3;
    int blocks[6][64];
    for (;;) {
        int result = decode_block(
            &source, end, blocks[0], depth < 2 ? mdec.env.iq_y : mdec.env.iq_c);
        if (result == 0)
            break;
        if (result < 0) {
            mdec.error = -1;
            break;
        }
        if (depth >= 2) {
            for (unsigned b = 1; b < 6; ++b) {
                if (decode_block(&source, end, blocks[b],
                                 b == 1 ? mdec.env.iq_c : mdec.env.iq_y) != 1) {
                    mdec.error = -1;
                    return;
                }
            }
        }
        unsigned bytes = depth == 0   ? 32
                         : depth == 1 ? 64
                         : depth == 2 ? 768
                                      : 512;
        if (!reserve(bytes)) {
            mdec.error = -1;
            return;
        }
        u8* out = mdec.output + mdec.length;
        if (depth < 2) {
            for (unsigned i = 0; i < 64; ++i) {
                unsigned v = (u8)clamp(blocks[0][i], -128, 127);
                if (!(mdec.command & (1u << 26)))
                    v ^= 128;
                if (depth)
                    out[i] = (u8)v;
                else if (!(i & 1))
                    out[i / 2] = (u8)(v >> 4);
                else
                    out[i / 2] |= (u8)(v & 0xf0);
            }
        } else {
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x) {
                    unsigned chroma = x / 2 + y / 2 * 8;
                    int cr = blocks[0][chroma], cb = blocks[1][chroma];
                    int luminance =
                        blocks[2 + x / 8 + y / 8 * 2][x % 8 + y % 8 * 8];
                    int color[3] = {
                        luminance + floor_shift(359 * cr + 128, 8),
                        luminance + floor_shift(-88 * cb - 183 * cr + 128, 8),
                        luminance + floor_shift(454 * cb + 128, 8)};
                    unsigned rgb[3];
                    for (unsigned c = 0; c < 3; ++c) {
                        rgb[c] = (u8)clamp(color[c], -128, 127);
                        if (!(mdec.command & (1u << 26)))
                            rgb[c] ^= 128;
                    }
                    unsigned pixel = x + y * 16;
                    if (depth == 2) {
                        for (unsigned c = 0; c < 3; ++c)
                            out[pixel * 3 + c] = (u8)rgb[c];
                    } else {
                        unsigned five[3];
                        for (unsigned c = 0; c < 3; ++c) {
                            unsigned value =
                                (u8)clamp(color[c], -128, 127) ^ 128;
                            five[c] = clamp((value + 4) >> 3, 0, 31);
                            if (mdec.command & (1u << 26))
                                five[c] ^= 16;
                        }
                        u16 packed =
                            (u16)(five[0] | five[1] << 5 | five[2] << 10 |
                                  ((mdec.command >> 10) & 0x8000));
                        out[pixel * 2] = (u8)packed;
                        out[pixel * 2 + 1] = (u8)(packed >> 8);
                    }
                }
        }
        mdec.length += bytes;
    }
}

void MDEC_reset(int mode) {
    if (mode != 0 && mode != 1) {
        mdec.error = -1;
        return;
    }
    mdec.length = mdec.cursor = 0;
    mdec.command = 0;
    mdec.error = 0;
    if (!mode)
        DecDCTGetEnv(&mdec.env);
}

void MDEC_in(void* buf, int size) {
    mdec.error = 0;
    if (!buf || size < 0) {
        mdec.error = -1;
        return;
    }
    const u8* data = (const u8*)buf;
    mdec.command = read32(data);
    data += 4;
    switch (mdec.command >> 29) {
    case 1: {
        mdec.length = mdec.cursor = 0;
        unsigned words = mdec.command & 0xffff;
        if ((unsigned)size < words) {
            mdec.error = -1;
            break;
        }
        decode(data, data + words * 4);
        break;
    }
    case 2: {
        unsigned bytes = (mdec.command & 1) ? 128 : 64;
        if ((unsigned)size < bytes / 4) {
            mdec.error = -1;
            break;
        }
        memcpy(mdec.env.iq_y, data, 64);
        if (bytes == 128)
            memcpy(mdec.env.iq_c, data + 64, 64);
        break;
    }
    case 3:
        if (size < 32) {
            mdec.error = -1;
            break;
        }
        for (unsigned i = 0; i < 64; ++i)
            mdec.env.dct[i] = (s16)read16(data + i * 2);
        break;
    default:
        break;
    }
    if (!mdec.error)
        Psyz_DmaComplete(0);
}

void MDEC_out(void* buf, int size) {
    if (!size)
        return;
    if (!buf || size < 0 || mdec.error ||
        (size_t)size > (mdec.length - mdec.cursor) / 4) {
        mdec.error = -1;
        return;
    }
    memcpy(buf, mdec.output + mdec.cursor, (size_t)size * 4);
    mdec.cursor += (size_t)size * 4;
    Psyz_DmaComplete(1);
}
int MDEC_in_sync(void) { return mdec.error; }
int MDEC_out_sync(void) { return mdec.error; }
u_long MDEC_status(void) {
    u32 status = 0x0004ffffu | ((mdec.command >> 2) & 0x07800000u);
    return status | (mdec.cursor == mdec.length ? 0x80000000u : 0x28000000u);
}

int DecDCTvlc(u_long* bs, u_long* buf) {
    NOT_IMPLEMENTED;
    return -1;
}
