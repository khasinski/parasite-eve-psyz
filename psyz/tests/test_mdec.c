#include "ztest.h"
#include <string.h>

#ifdef __psx__
static const uint32_t mdec_nonflat[] = {
    0x3800000F, 0x000413E8, 0x080207FD, 0x1020FE00, 0x040403FB, 0xFE000803,
    0x00091010, 0x080407FE, 0x13F0FE00, 0x040503F9, 0xFE000805, 0x00031020,
    0x080607FA, 0x13E0FE00, 0x040703FE, 0xFE000807,
};

static const uint8_t mdec_quant[64] = {
    2,  16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
    27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
    29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
    35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83};

static const uint16_t mdec_scale[64] = {
    0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82, 0x5a82,
    0x7d8a, 0x6a6d, 0x471c, 0x18f8, 0xe707, 0xb8e3, 0x9592, 0x8275,
    0x7641, 0x30fb, 0xcf04, 0x89be, 0x89be, 0xcf04, 0x30fb, 0x7641,
    0x6a6d, 0xe707, 0x8275, 0xb8e3, 0x471c, 0x7d8a, 0x18f8, 0x9592,
    0x5a82, 0xa57d, 0xa57d, 0x5a82, 0x5a82, 0xa57d, 0xa57d, 0x5a82,
    0x471c, 0x8275, 0x18f8, 0x6a6d, 0x9592, 0xe707, 0x7d8a, 0xb8e3,
    0x30fb, 0x89be, 0x7641, 0xcf04, 0xcf04, 0x7641, 0x89be, 0x30fb,
    0x18f8, 0xb8e3, 0x6a6d, 0x8275, 0x7d8a, 0x9592, 0x471c, 0xe707};

static uint32_t psx_mdec_read(unsigned offset) {
    return *(volatile uint32_t*)(0xBF801820u + offset);
}

static void psx_mdec_write(unsigned offset, uint32_t value) {
    *(volatile uint32_t*)(0xBF801820u + offset) = value;
}

static int hardware_transfer(
    uint32_t command, const uint32_t* data, unsigned count, uint32_t* output,
    unsigned output_words) {
    unsigned sent = 0, received = 0;
    for (unsigned poll = 0; poll < 0x100000; ++poll) {
        uint32_t status = psx_mdec_read(4);
        if (sent <= count && !(status & (sent ? 0x40000000u : 0x20000000u))) {
            psx_mdec_write(0, sent ? data[sent - 1] : command);
            ++sent;
        }
        if (received < output_words && !(status & 0x80000000u))
            output[received++] = psx_mdec_read(0);
        if (sent == count + 1 && received == output_words &&
            !(psx_mdec_read(4) & 0x20000000u))
            return 0;
    }
    zprintf("MDEC timeout: command=%08X status=%08X input=%u/%u output=%u/%u\n",
            command, psx_mdec_read(4), sent, count + 1, received, output_words);
    psx_mdec_write(4, 0x80000000u);
    return -1;
}

static void print_words(
    const char* name, const uint32_t* data, unsigned count) {
    zprintf("MDEC %s words=%u\n", name, count);
    for (unsigned i = 0; i < count; i += 8) {
        zprintf("%04X:", i);
        for (unsigned j = i; j < count && j < i + 8; ++j)
            zprintf(" %08X", data[j]);
        zprintf("\n");
    }
}

static void capture_hardware(
    uint32_t flags, const uint32_t* data, unsigned count,
    const uint8_t quant[128], const uint16_t scale[64]) {
    uint32_t padded[192];
    unsigned padded_count = (count + 31u) & ~31u;
    memcpy(padded, data, count * sizeof(*padded));
    for (unsigned i = count; i < padded_count; ++i)
        padded[i] = 0xFE00FE00u;
    data = padded;
    count = padded_count;
    uint32_t quant_words[32], scale_words[32];
    memcpy(quant_words, quant, sizeof(quant_words));
    memcpy(scale_words, scale, sizeof(scale_words));
    unsigned depth = (flags >> 27) & 3;
    const unsigned sizes[] = {8, 16, 192, 128};
    unsigned words = sizes[depth];
    uint32_t output[193], repeated[193];
    memset(output, 0xA5, sizeof(output));
    memset(repeated, 0xA5, sizeof(repeated));
    uint32_t command = 0x20000000 | flags | count;
    for (unsigned pass = 0; pass < 2; ++pass) {
        psx_mdec_write(4, 0x80000000u);
        zassert_s32_eq(
            0, hardware_transfer(0x40000001, quant_words, 32, NULL, 0));
        zassert_s32_eq(
            0, hardware_transfer(0x60000000, scale_words, 32, NULL, 0));
        zassert_s32_eq(0, hardware_transfer(command, data, count,
                                            pass ? repeated : output, words));
        psx_mdec_write(4, 0x80000000u);
        if (!pass) {
            zprintf("MDEC command=%08X output_order=pio\n", command);
            print_words("quant", quant_words, 32);
            print_words("scale", scale_words, 32);
            print_words("input", data, count);
            print_words("output", output, words);
        }
    }
    zexpect_u32_eq(0xA5A5A5A5, output[words]);
    zexpect_u32_eq(0xA5A5A5A5, repeated[words]);
    for (unsigned i = 0; i < words; ++i) {
        if (output[i] != repeated[i])
            zprintf("MDEC command=%08X word=%u first=%08X repeated=%08X\n",
                    command, i, output[i], repeated[i]);
        zassert_u32_eq(output[i], repeated[i]);
    }
}

static void hardware_tables(uint8_t quant[128], uint16_t scale[64]) {
    memcpy(quant, mdec_quant, 64);
    memcpy(quant + 64, mdec_quant, 64);
    memcpy(scale, mdec_scale, 128);
}

static unsigned generated_runlevels(
    uint32_t data[192], unsigned blocks, unsigned scale, int dense) {
    uint16_t coefficients[384];
    unsigned count = 0;
    for (unsigned block = 0; block < blocks; ++block) {
        unsigned first = (scale << 10) | (block & 1 ? 511 : 512);
        if (first == 0xFE00)
            ++first;
        coefficients[count++] = first;
        unsigned index = 0;
        for (unsigned i = 0; i < (dense ? 63 : 8); ++i) {
            unsigned run = dense ? 0 : (i + block) % 7;
            index += run + 1;
            if (index >= 64)
                break;
            unsigned level = (i * 173 + block * 97 + 513) & 1023;
            coefficients[count++] = (run << 10) | level;
        }
        if (!dense)
            coefficients[count++] = 0xFE00;
    }
    if (count & 1)
        coefficients[count++] = 0xFE00;
    for (unsigned i = 0; i < count / 2; ++i)
        data[i] =
            coefficients[i * 2] | ((uint32_t)coefficients[i * 2 + 1] << 16);
    return count / 2;
}

ZTEST(mdec, hardware_rgb24) {
    uint8_t quant[128];
    uint16_t scale[64];
    hardware_tables(quant, scale);
    capture_hardware(2u << 27, mdec_nonflat + 1, 15, quant, scale);
    capture_hardware(
        (2u << 27) | (1u << 26), mdec_nonflat + 1, 15, quant, scale);
}

ZTEST(mdec, hardware_rgb555_flags) {
    uint8_t quant[128];
    uint16_t scale[64];
    hardware_tables(quant, scale);
    for (unsigned flags = 0; flags < 4; ++flags)
        capture_hardware(
            (3u << 27) | (flags << 25), mdec_nonflat + 1, 15, quant, scale);
}

ZTEST(mdec, hardware_monochrome) {
    const uint32_t data[] = {0x00091010, 0x080407FE, 0xFE0013F0};
    uint8_t quant[128];
    uint16_t scale[64];
    hardware_tables(quant, scale);
    for (unsigned depth = 0; depth < 2; ++depth)
        for (unsigned sign = 0; sign < 2; ++sign)
            capture_hardware(
                (depth << 27) | (sign << 26), data, 3, quant, scale);
}

ZTEST(mdec, hardware_custom_quantization) {
    uint8_t quant[128];
    uint16_t scale[64];
    hardware_tables(quant, scale);
    for (unsigned i = 0; i < 128; ++i)
        quant[i] = (i * 17 + 3) & 255;
    capture_hardware(2u << 27, mdec_nonflat + 1, 15, quant, scale);
    capture_hardware(3u << 27, mdec_nonflat + 1, 15, quant, scale);
}

ZTEST(mdec, hardware_custom_scale) {
    uint8_t quant[128];
    uint16_t scale[64];
    hardware_tables(quant, scale);
    for (unsigned i = 0; i < 64; ++i)
        scale[i] ^= (i * 13 + 9) & 255;
    capture_hardware(2u << 27, mdec_nonflat + 1, 15, quant, scale);
    capture_hardware(3u << 27, mdec_nonflat + 1, 15, quant, scale);
}

ZTEST(mdec, hardware_generated_runlevels) {
    const unsigned scales[] = {1, 4, 63};
    uint8_t quant[128];
    uint16_t matrix[64];
    uint32_t data[192];
    hardware_tables(quant, matrix);
    for (unsigned s = 0; s < 3; ++s) {
        for (unsigned depth = 0; depth < 4; ++depth) {
            unsigned count =
                generated_runlevels(data, depth < 2 ? 1 : 6, scales[s], 0);
            for (unsigned sign = 0; sign < 2; ++sign)
                capture_hardware(
                    (depth << 27) | (sign << 26), data, count, quant, matrix);
        }
    }
}

ZTEST(mdec, hardware_zero_scale) {
    uint8_t quant[128];
    uint16_t matrix[64];
    uint32_t data[192];
    hardware_tables(quant, matrix);
    for (unsigned depth = 0; depth < 4; ++depth) {
        unsigned count = generated_runlevels(data, depth < 2 ? 1 : 6, 0, 0);
        for (unsigned sign = 0; sign < 2; ++sign)
            capture_hardware(
                (depth << 27) | (sign << 26), data, count, quant, matrix);
    }
}

ZTEST(mdec, hardware_full_blocks_without_end_codes) {
    uint8_t quant[128];
    uint16_t matrix[64];
    uint32_t data[192];
    hardware_tables(quant, matrix);
    for (unsigned depth = 0; depth < 4; ++depth) {
        unsigned count = generated_runlevels(data, depth < 2 ? 1 : 6, 4, 1);
        capture_hardware(depth << 27, data, count, quant, matrix);
    }
}
#endif

#ifndef __psx__
#include <libpress.h>
#include <libetc.h>
#include "../../decomp/src/libpress/libpress_private.h"

static unsigned input_calls, output_calls, callback_depth, maximum_depth;
static u32 callback_pixels[192];
static void input_complete(void) { ++input_calls; }
static void output_complete(void) {
    ++callback_depth;
    if (maximum_depth < callback_depth)
        maximum_depth = callback_depth;
    ++output_calls;
    if (output_calls == 1)
        DecDCTout((u_long*)(callback_pixels + 96), 96);
    --callback_depth;
}

ZTEST(mdec, flat_blocks_all_output_modes) {
    /* Zero coefficients decode to neutral gray, or zero in signed mode. */
    for (unsigned depth = 0; depth < 4; ++depth)
        for (unsigned sign = 0; sign < 2; ++sign)
            for (unsigned stp = 0; stp < 2; ++stp) {
                DecDCTReset(0);
                u32 input[7];
                unsigned words = depth < 2 ? 1 : 6;
                input[0] = (1u << 29) | (depth << 27) | (sign << 26) |
                           (stp << 25) | words;
                for (unsigned i = 1; i <= words; ++i)
                    input[i] = 0xfe000400;
                u32 output[194];
                memset(output, 0xa5, sizeof(output));
                const unsigned sizes[] = {8, 16, 192, 128};
                MDEC_in((u_long*)input, words);
                zexpect_s32_eq(0, DecDCTinSync(0));
                MDEC_out((u_long*)(output + 1), sizes[depth]);
                zexpect_s32_eq(0, DecDCToutSync(1));
                zexpect_u32_eq(0xa5a5a5a5, output[0]);
                zexpect_u32_eq(0xa5a5a5a5, output[1 + sizes[depth]]);
                u32 expected = sign ? 0 : depth == 0 ? 0x88888888 : 0x80808080;
                if (depth == 3) {
                    unsigned pixel = (sign ? 0 : 0x4210) | (stp ? 0x8000 : 0);
                    expected = pixel | pixel << 16;
                }
                for (unsigned i = 0; i < sizes[depth]; ++i)
                    zexpect_u32_eq(expected, output[1 + i]);
                zexpect_u32_eq(0x80000000, MDEC_status() & 0xa0000000u);
            }
}

ZTEST(mdec, callbacks_and_split_output) {
    DecDCTReset(0);
    input_calls = output_calls = callback_depth = maximum_depth = 0;
    DecDCTinCallback(input_complete);
    DecDCToutCallback(output_complete);
    u32 input[] = {0x38000006, 0xfe000400, 0xfe000400, 0xfe000400,
                   0xfe000400, 0xfe000400, 0xfe000400};
    DecDCTin((u_long*)input, 1); /* RGB24 */
    zexpect_u32_eq(1, input_calls);
    DecDCTout((u_long*)callback_pixels, 96);
    zexpect_u32_eq(2, output_calls);
    zexpect_u32_eq(1, maximum_depth);
    for (unsigned i = 0; i < 192; ++i)
        zexpect_u32_eq(0x80808080, callback_pixels[i]);
    zexpect_ptr_eq((void*)input_complete, (void*)DecDCTinCallback(NULL));
    zexpect_ptr_eq((void*)output_complete, (void*)DecDCToutCallback(NULL));
}

ZTEST(mdec, truncated_commands_and_output_do_not_write) {
    DecDCTReset(0);
    u32 data[] = {0x38000006, 0xfe000400, 0xfe000400};
    u32 output = 0xa5a5a5a5;
    MDEC_in((u_long*)data, 2);
    zexpect_s32_eq(-1, DecDCTinSync(0));
    MDEC_out((u_long*)&output, 1);
    zexpect_s32_eq(-1, DecDCToutSync(1));
    zexpect_u32_eq(0xa5a5a5a5, output);
    data[0] = 0x38000002;
    MDEC_in((u_long*)data, 2);
    zexpect_s32_eq(
        -1, DecDCTinSync(0)); /* two blocks cannot form a colored macroblock */
    DecDCTReset(1);
    MDEC_out((u_long*)&output, 1);
    zexpect_s32_eq(-1, DecDCToutSync(0));
    zexpect_u32_eq(0xa5a5a5a5, output);
}
#endif

#ifndef __psx__
ZTEST(mdec, quantization_and_scale_commands) {
    DECDCTENV original, changed;
    DecDCTReset(0);
    DecDCTGetEnv(&original);
    changed = original;
    u32 data[] = {0x38000006, 0xfe000400, 0xfe000400, 0xfe000420,
                  0xfe000420, 0xfe000420, 0xfe000420};
    u32 output[192];
    for (unsigned mode = 0; mode < 3; ++mode) {
        changed = original;
        if (mode == 1)
            changed.iq_y[0] = 4;
        if (mode == 2)
            memset(changed.dct, 0, sizeof(changed.dct));
        DecDCTPutEnv(&changed);
        if (mode == 2) {
            u32 scale[33] = {0x60000000};
            MDEC_in(scale, 32); /* dct in DECDCTENV is reserved by LIBPRESS. */
        }
        DecDCTin((u_long*)data, 1);
        DecDCTout((u_long*)output, 192);
        zexpect_s32_eq(0, DecDCToutSync(0));
        unsigned gray = mode == 0 ? 136 : mode == 1 ? 144 : 128;
        u32 expected = gray * 0x01010101u;
        for (unsigned i = 0; i < 192; ++i)
            zexpect_u32_eq(expected, output[i]);
    }
    DecDCTPutEnv(&original);
}
#endif

#ifndef __psx__
ZTEST(mdec, generated_streams_all_depths_and_output_guards) {
    u32 seed = 0x7c902341;
    for (unsigned sample = 0; sample < 128; ++sample) {
        DecDCTReset(0);
        u16 stream[4096];
        unsigned count = 0, depth = sample & 3;
        unsigned blocks = depth < 2 ? 8 : 48;
        for (unsigned b = 0; b < blocks; ++b) {
            seed = seed * 1664525u + 1013904223u;
            stream[count++] = (u16)((sample % 64) << 10 | (seed & 1023));
            unsigned position = 0;
            for (unsigned c = 0; c < 7; ++c) {
                seed = seed * 1664525u + 1013904223u;
                unsigned run = (seed >> 16) & 7;
                if (position + run + 1 >= 64)
                    break;
                stream[count++] = (u16)(run << 10 | (seed & 1023));
                position += run + 1;
            }
            stream[count++] = 0xfe00;
        }
        if (count & 1)
            stream[count++] = 0xfe00;
        u32 input[2049], output[1538];
        input[0] =
            1u << 29 | depth << 27 | ((sample >> 2) & 3) << 25 | count / 2;
        memcpy(input + 1, stream, count * 2);
        memset(output, 0xa5, sizeof(output));
        const unsigned words[] = {64, 128, 1536, 1024};
        MDEC_in(input, count / 2);
        zexpect_s32_eq(0, MDEC_in_sync());
        MDEC_out(output + 1, words[depth]);
        zexpect_s32_eq(0, MDEC_out_sync());
        zexpect_u32_eq(0xa5a5a5a5, output[0]);
        zexpect_u32_eq(0xa5a5a5a5, output[1 + words[depth]]);
        MDEC_out(output + 1 + words[depth], 1);
        zexpect_s32_eq(-1, MDEC_out_sync());
        zexpect_u32_eq(0xa5a5a5a5, output[1 + words[depth]]);
    }
}
#endif
