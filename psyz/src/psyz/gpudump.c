/* GPU command dump in the ps1dev GPUDUMP format (.psxgpu), as written and
 * replayed by DuckStation (https://github.com/ps1dev/standards GPUDUMP.md).
 *
 * A dump starts with the current display mode (GP1), the whole VRAM as one
 * GP0 A0h upload and the current drawing state (E1h..E6h), then records every
 * GP0 command that libgpu dispatches and every GP1 display command until the
 * dump ends with a VSync event. Replaying it in an emulator reproduces the
 * frame from the same VRAM, so the backend's VRAM after the frame can be
 * compared pixel for pixel with a reference GPU. */
#include <psyz.h>
#include <psyz/gpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../draw.h"
#include "../internal.h"
#include "gpudump.h"

enum {
    DUMP_GP0 = 0x00,
    DUMP_GP1 = 0x01,
    DUMP_VSYNC = 0x02,
    DUMP_GPU_VERSION = 0x06,
    DUMP_GAME_ID = 0x10,
    DUMP_VIDEO_FORMAT = 0x11,
    DUMP_COMMENT = 0x12,
    DUMP_TRACE_BEGIN = 0x05,
};

static FILE* dump_file;
static u32 draw_state[8] = {
    /* E0h unused, E1h..E6h as after GP1 00h reset */
    0, 0xE1000000, 0xE2000000, 0xE3000000, 0xE4000000, 0xE5000000, 0xE6000000,
};
static u32 display_state[9] = {
    0, 0, 0, 0x03000001, 0, 0x05000000, 0x06C60260, 0x07040010, 0x08000000,
};

int GpuDump_Active(void) { return dump_file != NULL; }

const u32* GpuDump_DrawState(void) { return draw_state; }

static void WriteWords(const u32* words, size_t count) {
    if (dump_file && count && fwrite(words, sizeof(u32), count, dump_file) != count) {
        WARNF("GPU dump write failed");
    }
}

static void WritePacket(unsigned type, const u32* words, size_t count) {
    u32 header = (u32)(count & 0xFFFFFF) | ((u32)type << 24);
    WriteWords(&header, 1);
    WriteWords(words, count);
}

static void WriteString(unsigned type, const char* text) {
    u32 words[16] = {0};
    size_t len = strlen(text);
    if (len > sizeof(words) - 1)
        len = sizeof(words) - 1;
    memcpy(words, text, len);
    WritePacket(type, words, len / 4 + 1);
}

void GpuDump_TrackDrawState(u32 word) {
    unsigned code = word >> 24;
    if (code >= 0xE1 && code <= 0xE6)
        draw_state[code - 0xE0] = word;
}

void GpuDump_TrackDisplay(u32 word) {
    unsigned op = (word >> 24) & 0x3F;
    if (op == 0) {
        /* GP1 00h resets the drawing state and the display */
        for (unsigned i = 1; i <= 6; i++)
            draw_state[i] = (0xE0u + i) << 24;
        display_state[3] = 0x03000001;
        display_state[8] = 0x08000000;
    } else if ((int)op < LEN(display_state)) {
        display_state[op] = (op << 24) | (word & 0xFFFFFF);
    }
    if (dump_file && (op == 0 || op == 3 || (op >= 5 && op <= 8)))
        WritePacket(DUMP_GP1, &word, 1);
}

void GpuDump_GP0(const u_long* words, int count) {
    if (!dump_file || count <= 0)
        return;
    u32 packet[64];
    while (count > 0) {
        int n = count < (int)LEN(packet) ? count : (int)LEN(packet);
        for (int i = 0; i < n; i++)
            packet[i] = (u32)words[i];
        WritePacket(DUMP_GP0, packet, (size_t)n);
        words += n;
        count -= n;
    }
}

void GpuDump_Image(const RECT* rect, const void* data) {
    if (!dump_file || rect->w <= 0 || rect->h <= 0)
        return;
    size_t pixels = (size_t)rect->w * (size_t)rect->h;
    size_t words = (pixels + 1) / 2;
    u32* packet = calloc(3 + words, sizeof(u32));
    if (!packet)
        return;
    packet[0] = 0xA0000000;
    packet[1] = (u32)(u16)rect->x | ((u32)(u16)rect->y << 16);
    packet[2] = (u32)(u16)rect->w | ((u32)(u16)rect->h << 16);
    memcpy(packet + 3, data, pixels * sizeof(u16));
    WritePacket(DUMP_GP0, packet, 3 + words);
    free(packet);
}

static int ReadVram(u16* vram) {
    RECT all = {0, 0, VRAM_W, VRAM_H};
    Psyz_GpuExeque();
    Draw_StoreImage(&all, (u_long*)vram);
    return 0;
}

int Psyz_GpuDumpBegin(const char* path) {
    static const u8 file_header[14] = {'P', 'S', 'X', 'G', 'P', 'U', 'D',
                                       'U', 'M', 'P', 'v', '1', 0,   0};
    if (dump_file)
        Psyz_GpuDumpEnd(NULL);
    u16* vram = malloc(VRAM_W * VRAM_H * sizeof(u16));
    if (!vram)
        return -1;
    ReadVram(vram);
    dump_file = fopen(path, "wb");
    if (!dump_file) {
        free(vram);
        return -1;
    }
    fwrite(file_header, 1, sizeof(file_header), dump_file);
    u32 version = 1; /* 1 MB VRAM, retail GPU */
    WritePacket(DUMP_GPU_VERSION, &version, 1);
    WriteString(DUMP_GAME_ID, "UNKNOWN");
    WriteString(DUMP_VIDEO_FORMAT, display_state[8] & 8 ? "PAL" : "NTSC");
    WriteString(DUMP_COMMENT, "Created by PsyZ.");
    for (int op = 3; op < LEN(display_state); op++) {
        if (op != 4)
            WritePacket(DUMP_GP1, &display_state[op], 1);
    }
    /* Uploads follow the mask setting: clear it for the VRAM snapshot. */
    u32 unmask = 0xE6000000;
    WritePacket(DUMP_GP0, &unmask, 1);
    {
        /* a zero size uploads all 1024x512 pixels */
        size_t words = VRAM_W * VRAM_H / 2;
        u32 hdr[4] = {(u32)((3 + words) & 0xFFFFFF) | (DUMP_GP0 << 24),
                      0xA0000000, 0, 0};
        WriteWords(hdr, 4);
        WriteWords((const u32*)vram, words);
    }
    free(vram);
    for (unsigned i = 1; i <= 6; i++)
        WritePacket(DUMP_GP0, &draw_state[i], 1);
    WritePacket(DUMP_TRACE_BEGIN, NULL, 0);
    return 0;
}

int Psyz_GpuDumpEnd(const char* vram_path) {
    if (!dump_file)
        return -1;
    Psyz_GpuExeque();
    u32 ticks[2] = {0, 0};
    WritePacket(DUMP_VSYNC, ticks, 2);
    fclose(dump_file);
    dump_file = NULL;
    if (!vram_path)
        return 0;
    u16* vram = malloc(VRAM_W * VRAM_H * sizeof(u16));
    if (!vram)
        return -1;
    ReadVram(vram);
    FILE* f = fopen(vram_path, "wb");
    int ok = f && fwrite(vram, sizeof(u16), VRAM_W * VRAM_H, f) == VRAM_W * VRAM_H;
    if (f)
        fclose(f);
    free(vram);
    return ok ? 0 : -1;
}
