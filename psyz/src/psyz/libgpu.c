#include <psyz/gpu.h>
#include <psyz.h>
#ifdef PE_PS5_GPU_NATIVE
#include <psyz/video.h>
#endif
#include <libgpu.h>
#include <psyz/gte.h>
#include <psyz/gpu.h>
#include "../../../decomp/src/libgpu/libgpu_private.h"
#include <psyz/log.h>
#include "../draw.h"
#include "../internal.h"
#include "gpudump.h"
#include <stdlib.h>
#include <string.h>

// The GPU is a FIFO: the register reflects what it has consumed by the GPU the
// moment it's queried. The problem with the current PsyZ implementation is
// the GPU emulation is synchronous. GPU packets are accumulated in GPU_Enqueue
// then consumed by Psyz_GpuExeque on demand by the same thread. This means the
// values of GPU_STATUS will be stale, especially when 0xE1xxxxxx gets enqueued
// and then GPU_STATUS gets queried before Psyz_GpuExeque is called. But then
// calling Psyz_GpuExeque implies synchronization, which slows down performance.
static u32 GPU_STATUS = 0;

typedef enum {
    // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gpu-versions
    GPU_V0, // 1MB VRAM, retail
    GPU_V1, // 2MB VRAM, arcade
    GPU_V2, // 2MB VRAM, PSone
} GpuVersion;

static void GPU_clear_cache() { Draw_ClearCache(); }

// Optional CPU copy of the VRAM, kept from image uploads, fills and copies
// (Psyz_GpuSetVramCopy). Primitives are not rasterized into it: the first
// primitive drawn after the drawing area changes marks that area unknown.
#define VRAM_COPY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define VRAM_COPY_MAX(a, b) ((a) > (b) ? (a) : (b))
static u16* vram_copy = NULL;
static u8* vram_copy_known = NULL;
static int vram_area_x0, vram_area_y0, vram_area_x1, vram_area_y1;
static int vram_area_marked = 0;

int Psyz_GpuSetVramCopy(int enabled) {
    vram_area_marked = 0;
    if (!enabled) {
        free(vram_copy);
        free(vram_copy_known);
        vram_copy = NULL;
        vram_copy_known = NULL;
        return 0;
    }
    if (vram_copy) {
        return 0;
    }
    vram_copy = calloc(VRAM_W * VRAM_H, sizeof(u16));
    vram_copy_known = calloc(VRAM_W * VRAM_H, 1);
    if (!vram_copy || !vram_copy_known) {
        Psyz_GpuSetVramCopy(0);
        return -1;
    }
    return 0;
}

const unsigned short* Psyz_GpuGetVramCopy(const unsigned char** known) {
    if (known) {
        *known = vram_copy_known;
    }
    return vram_copy;
}

static int VramCopyClip(RECT* rect) {
    int x0 = VRAM_COPY_MAX(rect->x, 0), y0 = VRAM_COPY_MAX(rect->y, 0);
    int x1 = VRAM_COPY_MIN(rect->x + rect->w, VRAM_W);
    int y1 = VRAM_COPY_MIN(rect->y + rect->h, VRAM_H);
    if (!vram_copy || x0 >= x1 || y0 >= y1) {
        return 0;
    }
    rect->x = (short)x0;
    rect->y = (short)y0;
    rect->w = (short)(x1 - x0);
    rect->h = (short)(y1 - y0);
    return 1;
}

static void VramCopyLoad(const RECT* rect, const u_long* data) {
    // same bounds rule as the backends: a rect past the VRAM is not uploaded
    if (!vram_copy || rect->x < 0 || rect->y < 0 || rect->w <= 0 ||
        rect->h <= 0 || rect->x + rect->w > VRAM_W ||
        rect->y + rect->h > VRAM_H) {
        return;
    }
    vram_area_marked = 0;
    const u16* src = (const u16*)data;
    for (int j = 0; j < rect->h; j++) {
        size_t row = (size_t)(rect->y + j) * VRAM_W + rect->x;
        memcpy(
            vram_copy + row, src + (size_t)j * rect->w, rect->w * sizeof(u16));
        memset(vram_copy_known + row, 1, rect->w);
    }
}

static void VramCopyFill(RECT rect, u_char r, u_char g, u_char b) {
    if (!VramCopyClip(&rect)) {
        return;
    }
    vram_area_marked = 0;
    u16 c = (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
    for (int j = 0; j < rect.h; j++) {
        size_t row = (size_t)(rect.y + j) * VRAM_W + rect.x;
        for (int i = 0; i < rect.w; i++) {
            vram_copy[row + i] = c;
        }
        memset(vram_copy_known + row, 1, rect.w);
    }
}

static void VramCopyMove(RECT rect, int x, int y) {
    RECT dst = {(short)x, (short)y, rect.w, rect.h};
    if (!VramCopyClip(&rect) || !VramCopyClip(&dst)) {
        return;
    }
    vram_area_marked = 0;
    int w = VRAM_COPY_MIN(rect.w, dst.w), h = VRAM_COPY_MIN(rect.h, dst.h);
    for (int j = 0; j < h; j++) {
        int sj = j; // the GPU copies rows top to bottom, even overlapping
        size_t s = (size_t)(rect.y + sj) * VRAM_W + rect.x;
        size_t d = (size_t)(dst.y + sj) * VRAM_W + dst.x;
        memmove(vram_copy + d, vram_copy + s, w * sizeof(u16));
        memmove(vram_copy_known + d, vram_copy_known + s, w);
    }
}

static void VramCopyDrawn(void) {
    if (!vram_copy || vram_area_marked) {
        return;
    }
    vram_area_marked = 1;
    RECT rect = {(short)vram_area_x0, (short)vram_area_y0,
                 (short)(vram_area_x1 - vram_area_x0 + 1),
                 (short)(vram_area_y1 - vram_area_y0 + 1)};
    if (rect.w > 0 && rect.h > 0 && VramCopyClip(&rect)) {
        for (int j = 0; j < rect.h; j++) {
            memset(vram_copy_known + (size_t)(rect.y + j) * VRAM_W + rect.x, 0,
                   rect.w);
        }
    }
}


/* VRAM transfers address the VRAM modulo 1024x512: a rectangle that runs
 * past the right or bottom edge wraps to the left or top. Backends and the
 * CPU copy take non-wrapping pieces; these helpers cut a transfer at the
 * edges. Coordinates and sizes are already reduced to the GPU's ranges. */
typedef struct {
    int x, y, w, h;   /* VRAM piece */
    int ox, oy;       /* offset of the piece inside the transfer */
} VramPiece;

static int VramPieces(int x, int y, int w, int h, VramPiece* out) {
    int n = 0;
    for (int oy = 0; oy < h;) {
        int py = (y + oy) % VRAM_H, ph = VRAM_COPY_MIN(h - oy, VRAM_H - py);
        for (int ox = 0; ox < w;) {
            int px = (x + ox) % VRAM_W, pw = VRAM_COPY_MIN(w - ox, VRAM_W - px);
            out[n++] = (VramPiece){px, py, pw, ph, ox, oy};
            ox += pw;
        }
        oy += ph;
    }
    return n;
}

static u32 gpu_mask_setting; /* last E6h: bit 0 set mask, bit 1 check mask */

static void VramFill(const u_long* words) {
    /* GP0 02h: X in 16-pixel units, width rounded up to 16, mask ignored */
    u32 xy = (u32)words[1], wh = (u32)words[2], color = (u32)words[0];
    int x = (int)(xy & 0x3F0), y = (int)((xy >> 16) & 0x1FF);
    int w = (int)(((wh & 0x3FF) + 0xF) & ~0xFu), h = (int)((wh >> 16) & 0x1FF);
    VramPiece piece[4];
    int n = w && h ? VramPieces(x, y, w, h, piece) : 0;
    for (int i = 0; i < n; i++) {
        RECT rect = {(short)piece[i].x, (short)piece[i].y, (short)piece[i].w,
                     (short)piece[i].h};
        u_char r = (u_char)(color & 0xFF), g = (u_char)((color >> 8) & 0xFF),
               b = (u_char)((color >> 16) & 0xFF);
        VramCopyFill(rect, r, g, b);
        Draw_ClearImage(&rect, r, g, b);
    }
}

static void VramMove(const u_long* words) {
    /* GP0 80h; DuckStation cuts copies running past an edge the same way */
    u32 src = (u32)words[1], dst = (u32)words[2], wh = (u32)words[3];
    int sx = (int)(src & 0x3FF), sy = (int)((src >> 16) & 0x1FF);
    int dx = (int)(dst & 0x3FF), dy = (int)((dst >> 16) & 0x1FF);
    int w = (int)(wh & 0x3FF), h = (int)((wh >> 16) & 0x1FF);
    if (!w)
        w = VRAM_W;
    if (!h)
        h = VRAM_H;
    if (sx == dx && sy == dy && !(gpu_mask_setting & 1))
        return;
    for (int oy = 0; oy < h;) {
        int csy = (sy + oy) % VRAM_H, cdy = (dy + oy) % VRAM_H;
        int rows = VRAM_COPY_MIN(h - oy, VRAM_COPY_MIN(VRAM_H - csy, VRAM_H - cdy));
        for (int ox = 0; ox < w;) {
            int csx = (sx + ox) % VRAM_W, cdx = (dx + ox) % VRAM_W;
            int cols = VRAM_COPY_MIN(w - ox, VRAM_COPY_MIN(VRAM_W - csx, VRAM_W - cdx));
            RECT rect = {(short)csx, (short)csy, (short)cols, (short)rows};
            VramCopyMove(rect, cdx, cdy);
            Draw_MoveImage(&rect, (unsigned)cdx, (unsigned)cdy);
            ox += cols;
        }
        oy += rows;
    }
}

/* Image transfers keep the caller's rectangle layout; out of range
 * coordinates wrap. Returns the number of pieces, 0 for an empty transfer. */
static int VramTransferPieces(const RECT* rect, VramPiece* piece) {
    if (rect->w <= 0 || rect->h <= 0 || rect->w > VRAM_W || rect->h > VRAM_H)
        return 0;
    return VramPieces(rect->x & (VRAM_W - 1), rect->y & (VRAM_H - 1), rect->w,
                      rect->h, piece);
}

static void VramTransfer(RECT* rect, u_long* data, int store) {
    VramPiece piece[4];
    int n = VramTransferPieces(rect, piece);
    if (n == 1) {
        RECT r = {(short)piece[0].x, (short)piece[0].y, rect->w, rect->h};
        if (store) {
            Draw_StoreImage(&r, data);
        } else {
            VramCopyLoad(&r, data);
            Draw_LoadImage(&r, data);
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        u16* image = (u16*)data;
        u16* part = malloc((size_t)piece[i].w * piece[i].h * sizeof(u16));
        RECT r = {(short)piece[i].x, (short)piece[i].y, (short)piece[i].w,
                  (short)piece[i].h};
        if (!part)
            return;
        if (!store)
            for (int j = 0; j < piece[i].h; j++)
                memcpy(part + (size_t)j * piece[i].w,
                       image + (size_t)(piece[i].oy + j) * rect->w + piece[i].ox,
                       (size_t)piece[i].w * sizeof(u16));
        if (store) {
            Draw_StoreImage(&r, (u_long*)part);
            for (int j = 0; j < piece[i].h; j++)
                memcpy(image + (size_t)(piece[i].oy + j) * rect->w + piece[i].ox,
                       part + (size_t)j * piece[i].w,
                       (size_t)piece[i].w * sizeof(u16));
        } else {
            VramCopyLoad(&r, (u_long*)part);
            Draw_LoadImage(&r, (u_long*)part);
        }
        free(part);
    }
}

static void GPU_write_image() { NOT_IMPLEMENTED; }

static void GPU_read_image() { NOT_IMPLEMENTED; }

static int queue_len = 0;
static u_long queue_buf[0x4000];

typedef struct {
    PsyzGpuCommandHandler handler;
    void* userdata;
} UserGpuCommand;

static UserGpuCommand user_gpu_commands[256];

int Psyz_GpuRegisterCommandHandler(
    unsigned int opcode, PsyzGpuCommandHandler handler, void* userdata) {
    if (opcode > 0xFF) {
        return -1;
    }
    user_gpu_commands[opcode].handler = handler;
    user_gpu_commands[opcode].userdata = handler ? userdata : NULL;
    return 0;
}

int Psyz_GpuRegisterCommandHandlerIfVacant(unsigned int opcode,PsyzGpuCommandHandler handler,void *data){
    if(opcode>255||!handler||(user_gpu_commands[opcode].handler&&user_gpu_commands[opcode].handler!=handler))return -1;
    return Psyz_GpuRegisterCommandHandler(opcode,handler,data);
}

int Psyz_GpuSetHorizontalGrid(
    unsigned int source_width, unsigned int target_width) {
    return Draw_SetHorizontalGrid(source_width, target_width);
}

static void DispatchPackets(u_long* buf, int len) {
    for (int i = 0; i < len; i++) {
        u_long op = buf[i];
        int code = (int)(buf[i] >> 24) & 0xFF;
        int start = i;
        GpuDump_TrackDrawState((u32)op);
        // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gpu-render-polygon-commands
        switch (code) {
        case 0x00:
            // empty?!
            break;
        case 0x01: // clear cache
            GPU_clear_cache();
            break;
        case 0x02: // frame buffer rectangle draw
            VramFill(&buf[i]);
            i += 2;
            break;
        case 0x80: // move image
            VramMove(&buf[i]);
            i += 3;
            break;
        case 0xA0: // write image
            GPU_write_image();
            break;
        case 0xC0: // read image
            GPU_read_image();
            break;
        case 0xE1:
            GPU_STATUS = (GPU_STATUS & ~0x7FF) | (u32)(op & 0x7FF);
            Draw_SetTexpageMode((ParamDrawTexpageMode*)&op);
            break;
        case 0xE2:
            // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gp0e2h-texture-window-setting
            Draw_SetTextureWindow(op & 0x1F, (op >> 5) & 0x1F,
                                  (op >> 10) & 0x1F, (op >> 15) & 0x1F);
            break;
        case 0xE3:
            vram_area_x0 = (int)op & 0x3FF;
            vram_area_y0 = (int)(op >> 10) & 0x3FF;
            vram_area_marked = 0;
            Draw_SetAreaStart((int)op & 0x3FF, (int)(op >> 10) & 0x3FF);
            break;
        case 0xE4:
            vram_area_x1 = (int)op & 0x3FF;
            vram_area_y1 = (int)(op >> 10) & 0x3FF;
            vram_area_marked = 0;
            Draw_SetAreaEnd((int)op & 0x3FF, (int)(op >> 10) & 0x3FF);
            break;
        case 0xE5:
            Draw_SetOffset((int)op & 0x7FF, (int)(op >> 11) & 0x7FF);
            break;
        case 0xE6:
            gpu_mask_setting = (u32)op & 3;
            Draw_SetMask(!!(op & 1), !!(op & 2));
            break;
        default:
            if (code >= 0x20 && code < 0x80) {
                VramCopyDrawn();
                i += Draw_PushPrim(&buf[i], len - i) - 1;
                break;
            }
            if (user_gpu_commands[code].handler) {
                int consumed = user_gpu_commands[code].handler(
                    &buf[i], len - i, user_gpu_commands[code].userdata);
                if (consumed > 0 && consumed <= len - i) {
                    i += consumed - 1;
                    break;
                }
                WARNF("custom command %02X returned invalid length %d", code,
                      consumed);
                break;
            }
            WARNF("unsupported command %02X", code);
        }
        /* Application-defined commands are not PS1 GPU commands. */
        if (GpuDump_Active() && (code <= 0x02 || (code >= 0x20 && code <= 0x80) ||
                                 (code >= 0xE1 && code <= 0xE6)))
            GpuDump_GP0(&buf[start], i - start + 1);
    }
}

void Psyz_GpuResendDrawState(void) {
    const u32* state = GpuDump_DrawState();
    u_long words[6];
    for (int i = 0; i < 6; i++)
        words[i] = state[i + 1];
    DispatchPackets(words, 6);
}

int Psyz_GpuExeque() {
    if (sizeof(void*) == 4) {
        // 32-bit targets dispatch straight into the draw buffer
        Draw_FlushBuffer();
    }
    Draw_ResetBuffer();
    if (queue_len > 0) {
        DispatchPackets(queue_buf, queue_len);
        Draw_FlushBuffer();
    }
    int sync_result=Draw_ExequeSync();
    queue_len = 0;
#ifdef PE_PS5_GPU_NATIVE
    /* Original OT admission may already have drained complete chunks.
     * Even an empty remaining queue is now fully consumed, never a GPU fence. */
    Psyz_VideoMovieQueueConsumed(sync_result==0);
#else
    (void)sync_result;
#endif
    return queue_len;
}

/* Native DR_* arrays contain u_long words, while combined packets retain
 * packed 32-bit GP0 words. MargePrim's reserved NOP makes the format explicit
 * without changing the tag ABI or relying on the first command being a shape.
 */
static int is_packed_command_stream(const u32* words, int count) {
    if (count <= 0)
        return 0;
    unsigned int code = words[0] >> 24;
    if (code >= 0x20 && code < 0x80)
        return 1;
    for (int i = 0; i < count; ++i)
        if (words[i] == PSYZ_MERGED_PRIM_NOP)
            return 1;
    return 0;
}

static void GPU_Enqueue(u_long* packets) {
    DR_ENV* env = (DR_ENV*)packets;
    if (sizeof(u_long) == 4) {
        // fast path for 32-bit systems
        while (1) {
            if (env->len > 0) {
                DispatchPackets((u_long*)env->code, (int)env->len);
            }
            if (isendprim(env)) {
                break;
            }
            env = (DR_ENV*)nextPrim(env);
        }
#ifdef __EMSCRIPTEN__
        Draw_FlushBuffer();
#endif
        return;
    }
    while (1) {
        if (queue_len + env->len > LEN(queue_buf)) {
            if (env->len > 0x100) {
                ERRORF("packet 0x%X long, likely corrupted", env->len);
            }
            INFOF("GPU queue full, calling exeque");
            Psyz_GpuExeque();
        }
        // a flush empties the queue; the packet that did not fit still goes in
        if (sizeof(u_long) == 8) {
            // Wow okay, this part is uuuugly...
            // Gpu code is usually written to a u_long array, which will work
            // fine on both 32-bit and 64-bit compiled code.
            // But primitives are mapped from structs, we need to align the data
            if (is_packed_command_stream(
                    (const u32*)env->code, (int)env->len)) {
                // it is a prim, we need to split
                u32* prim_data = (u32*)env->code;
                for (u_long i = 0; i < env->len; i++) {
                    queue_buf[queue_len + i] = prim_data[i];
                    Psyz_GpuCopyInterpolationPacket(&prim_data[i],&queue_buf[queue_len+i]);
                    Psyz_GteCopyQueuedPgxpSxy(
                        &prim_data[i], &queue_buf[queue_len + i], prim_data[i]);
                }
            } else if (env->len > 0) {
                memcpy(queue_buf + queue_len, env->code,
                       env->len * sizeof(u_long));
                for(u_long i=0;i<env->len;++i)Psyz_GpuCopyInterpolationPacket((u_long *)env->code+i,&queue_buf[queue_len+i]);
            }
        }
        queue_len += (int)env->len;
        if (isendprim(env)) {
            break;
        }
        env = (DR_ENV*)nextPrim(env);
    }
}

// Weird fix due to addque2 calling the callback with two u_long params
static int GPU_EnqueueTrampoline(u_long p1, u_long p2) {
    GPU_Enqueue((u_long*)p1);
    return 0;
}

/* PSY-Q's transfers (_dws, _drs) clear the CLUT cache (GP0 01h) first */
static void GPU_TransferClearCache(void) {
    u_long clear = 0x01000000;
    Draw_ClearCache();
    GpuDump_GP0(&clear, 1);
}

static int GPU_DataWrite(RECT* rect, u_long* data) {
    Psyz_GpuExeque();
    GPU_TransferClearCache();
    GpuDump_Image(rect, data);
    VramTransfer(rect, data, 0);
    return 0;
}
static int GPU_DataRead(RECT* rect, u_long* data) {
    Psyz_GpuExeque();
    GPU_TransferClearCache();
    VramTransfer(rect, data, 1);
    return 0;
}
static int psyz_cwb(u32* data, s32 n) {
    if (n <= 0)
        return 0;
    if (!data || (size_t)n > LEN(queue_buf))
        return -1;
    Psyz_GpuExeque();
    /* DrawPrim uses the same mixed ABI as OT packets: packed 32-bit
     * primitive fields, native command words for DR_* packets. It must
     * also dispatch E1/E3/etc commands rather than treating them as shapes. */
    if (sizeof(u_long) == 8 && is_packed_command_stream(data, n)) {
        for (int i = 0; i < n; ++i) {
            queue_buf[i] = data[i];
            Psyz_GpuCopyInterpolationPacket(&data[i],&queue_buf[i]);
            Psyz_GteCopyQueuedPgxpSxy(&data[i], &queue_buf[i], data[i]);
        }
    } else {
        memcpy(queue_buf, data, (size_t)n * sizeof(u_long));
        for (int i = 0; i < n; ++i){
            Psyz_GpuCopyInterpolationPacket((u_long *)data+i,&queue_buf[i]);
            Psyz_GteCopyQueuedPgxpSxy(
                (u_long*)data + i, &queue_buf[i], (unsigned)queue_buf[i]);
        }
    }
    queue_len = n;
    return Psyz_GpuExeque();
}

static int _param(int x) { return 0; }
static int psyz_addque2(
    int (*exec)(u_long p1, u_long p2), u_long p1, int len, u_long p2) {
    return exec(p1, p2);
}
static int psyz_addque(
    int (*exec)(u_long p1, u_long p2), u_long p1, u_long p2) {
    return psyz_addque2(exec, p1, 0, p2);
}

// psyz_clr is very similar to _clr from libgpu/sys.c
static DR_ENV clear_cmd;
static int psyz_clr(RECT* rect, u32 color) {
    rect->w = CLAMP(rect->w, 0, VRAM_W - 1);
    rect->h = CLAMP(rect->h, 0, VRAM_H - 1);

#if 1 // HACK: faster but more inaccurate path, doesn't need a Psyz_GpuExeque
    setlen(&clear_cmd, 4);
    clear_cmd.code[0] = 0xE6000000; // mask bit setting
    clear_cmd.code[1] = (color & 0xFFFFFF) | 0x02000000;
    clear_cmd.code[2] = (u_long) * (u32*)&rect->x;
    clear_cmd.code[3] = (u_long) * (u32*)&rect->w;
#else
    // Needs a sync so 0xE1 CMDs can update GPU_STATUS before it gets used.
    Psyz_GpuExeque();
    setlen(&clear_cmd, 5);
    clear_cmd.code[0] = 0xE6000000; // mask bit setting
    clear_cmd.code[1] = 0xE1000000 | GPU_STATUS & 0x7FF | (color >> 0x1F) << 10;
    clear_cmd.code[2] = (color & 0xFFFFFF) | 0x02000000;
    clear_cmd.code[3] = (u_long) * (u32*)&rect->x;
    clear_cmd.code[4] = (u_long) * (u32*)&rect->w;
#endif

    termPrim(&clear_cmd);
    GPU_Enqueue((u_long*)&clear_cmd);
    return 0;
}

/* The CRTC's vertical display range, which places the vblank interrupt
 * (Psyz_AudioDisplayRangeChanged). A GPU reset sets lines 16..255. */
enum { CRTC_RESET_Y1 = 0x10, CRTC_RESET_Y2 = 0xFF };
static unsigned crtc_y1 = CRTC_RESET_Y1, crtc_y2 = CRTC_RESET_Y2;

static void CrtcVerticalRange(unsigned y1, unsigned y2) {
    if (y1 == crtc_y1 && y2 == crtc_y2) {
        return;
    }
    Psyz_AudioDisplayRangeChanged(crtc_y1, crtc_y2, y1, y2);
    crtc_y1 = y1;
    crtc_y2 = y2;
}

/* GPUSTAT bit 31, which libetc's VSync waits on in interlace: in 480-line
 * interlace (GP1(08h) bits 2 and 5) it is the displayed field's line parity,
 * (display y + display field) & 1, during the display range and y & 1 in
 * the blank; in other modes it flips every line. As in DuckStation's CRTC
 * the field flips at the start of each field (line 0, the end of the blank
 * lines after the display range) while vertical interlace (bit 5) is on and
 * is 0 otherwise, and the displayed field is latched at each vblank start:
 * the field after the flip in 480-line interlace, else 0. A mode set in the
 * blank, as a PutDispEnv right after VSync, counts for that flip but not
 * for the latched field, so the first two interlaced fields show the same
 * parity, unless a moved display range starts another vblank before line 0
 * (Psyz_AudioDisplayRangeChanged). */
static unsigned crtc_display_y, crtc_mode, crtc_field, crtc_display_field;
static int crtc_flip_pending; // the next field start after the last vblank
static uint64_t crtc_flip_us; // its time

static void CrtcFieldStart(void) {
    crtc_flip_pending = 0;
    crtc_field = crtc_mode & 0x20 ? crtc_field ^ 1 : 0;
}

int Psyz_GpuCrtcInterlaced480(void) { return (crtc_mode & 0x24) == 0x24; }

unsigned Psyz_GpuCrtcFieldLines(void) { return crtc_mode & 8 ? 314 : 263; }

static int ntsc_pal_picture;
void Psyz_GpuSetNtscPalPicture(int enabled) { ntsc_pal_picture = enabled != 0; }
int Psyz_GpuGetNtscPalPicture(void) { return ntsc_pal_picture; }

unsigned Psyz_GpuCrtcBlankLines(void) {
    unsigned total = Psyz_GpuCrtcFieldLines();
    unsigned y1 = crtc_y1 < total ? crtc_y1 : total;
    unsigned y2 = crtc_y2 < total ? crtc_y2 : total;
    return y2 > y1 ? total - y2 + y1 : 0;
}

int Psyz_GpuCrtcOddLine(unsigned line) {
    if (!Psyz_GpuCrtcInterlaced480()) {
        return (int)((crtc_display_y + line) & 1);
    }
    if (line < Psyz_GpuCrtcBlankLines()) {
        return (int)(crtc_display_y & 1);
    }
    return (int)((crtc_display_y + crtc_display_field) & 1);
}

void Psyz_GpuCrtcBlank(void) {
    uint64_t now = Psyz_KernelNowUs();
    if (crtc_flip_pending && now >= crtc_flip_us) {
        CrtcFieldStart();
    }
    crtc_display_field = Psyz_GpuCrtcInterlaced480() ? crtc_field ^ 1 : 0;
    unsigned total = Psyz_GpuCrtcFieldLines();
    unsigned lines = crtc_y2 < total ? total - crtc_y2 : 0;
    uint64_t line_ns = total == 314 ? 64017 : 63563;
    crtc_flip_pending = 1;
    crtc_flip_us = now + lines * line_ns / 1000;
}

/* A GPU reset leaves the display mode of the console's region: a PAL
 * console's CRTC runs 50 Hz fields from the start (DuckStation's GPUSTAT
 * reset). PSY-Q games name it with SetVideoMode (Psyz_GpuConsoleVideoMode),
 * which also sets the fields before the game's first display mode. */
static int console_pal, display_mode_set;

static void CrtcResetMode(void) {
    unsigned mode = console_pal ? 8 : 0;
    crtc_mode = mode;
    Draw_SetDisplayMode((DisplayMode*)&mode);
    Psyz_AudioVideoModeChanged(console_pal);
}

void Psyz_GpuConsoleVideoMode(int pal) {
    console_pal = pal;
    if (!display_mode_set) {
        CrtcResetMode();
    }
}

void Psyz_GpuCrtcReset(void) {
    CrtcVerticalRange(CRTC_RESET_Y1, CRTC_RESET_Y2);
    crtc_display_y = 0;
    crtc_field = 0;
    crtc_display_field = 0;
    crtc_flip_pending = 0;
    CrtcResetMode();
}

void Psyz_GpuDisplayCommand(unsigned int cmd) {
    unsigned char op = (cmd >> 24) & 0x3F;
    GpuDump_TrackDisplay(cmd);
    switch (op) {
    case 0:
        Draw_Reset();
        Psyz_GpuCrtcReset();
        break;
    case 1:
        LOG_ONCE("Reset FIFO not implemented");
        break;
    case 2:
        LOG_ONCE("Ack IRQ not implemented");
        break;
    case 3:
        Draw_DisplayEnable(!(cmd & 1));
        break;
    case 4:
        LOG_ONCE("DMA direction not implemented");
        break;
    case 5:
        crtc_display_y = (cmd >> 10) & 0x3FF;
        Draw_DisplayArea(cmd & 0x3FF, (cmd >> 10) & 0x3FF);
        break;
    case 6:
        Draw_DisplayHorizontalRange(cmd & 0xFFF, (cmd >> 12) & 0xFFF);
        break;
    case 7:
        CrtcVerticalRange(cmd & 0x3FF, (cmd >> 10) & 0x3FF);
        Draw_DisplayVerticalRange(cmd & 0x3FF, (cmd >> 10) & 0x3FF);
        break;
    case 8:
        cmd &= 0xFFFFFF;
        if (crtc_flip_pending && Psyz_KernelNowUs() >= crtc_flip_us) {
            CrtcFieldStart(); // the field started in the old mode
        }
        crtc_mode = cmd & 0xFF;
        display_mode_set = 1;
        Psyz_AudioVideoModeChanged((cmd & 8) != 0);
        Draw_SetDisplayMode((DisplayMode*)&cmd);
        break;
    default:
        WARNF("unhandled ctl %02X (%08X)", op, cmd);
        break;
    }
}
static int psyz_getctl(int _) {
    NOT_IMPLEMENTED;
    return 0;
}

static int psyz_otc(OT_TYPE* ot, s32 n) {
    s32 i;

    if (n <= 0) {
        return 0;
    }
    for (i = n - 1; i > 0; i--) {
        setaddr(&ot[i], &ot[i - 1]);
        setlen(&ot[i], 0);
    }
    setaddr(&ot[0], 0xFFFFFF);
    setlen(&ot[0], 0);
    return 0;
}

static int psyz_param(int _) {
    NOT_IMPLEMENTED;
    return 0;
}
static int psyz_reset(int _) { return GPU_V0; }
static u_long psyz_status(void) {
    NOT_IMPLEMENTED;
    return 0;
}
static int psyz_sync(int mode) {
    // see decomp/src/libgpu/sys.c
    // mode 0 waits until all the queue is drawn on screen
    // mode 1 process the queue and return how many elements have been queued
    // return -1 if GPU has timed out
    // but on PC the implementation is much simpler as it's always synced
    Psyz_GpuExeque();
    return 0;
}

int psyz_gpu_version(int mode) { return GPU_V0; }

// forwards raw GP0 words into the internal queue, then trigger execution.
void Psyz_GpuWriteGP0(unsigned int word) {
    if (queue_len >= (int)LEN(queue_buf)) {
        WARNF("GPU queue full");
        return;
    }
    queue_buf[queue_len++] = (u_long)word;
}

void GPU_cw(u_long* param) {
    GpuVtable* gpu = (GpuVtable*)param;
    gpu->ver = "psyz";
    gpu->addque = psyz_addque;
    gpu->addque2 = psyz_addque2;
    gpu->clr = psyz_clr;
    gpu->ctl = Psyz_GpuDisplayCommand;
    gpu->cwb = psyz_cwb;
    gpu->cwc = (void (*)(u_long*))GPU_EnqueueTrampoline;
    gpu->drs = GPU_DataRead;
    gpu->dws = GPU_DataWrite;
    gpu->exeque = Psyz_GpuExeque;
    gpu->getctl = psyz_getctl;
    gpu->otc = psyz_otc;
    gpu->param = psyz_param;
    gpu->reset = psyz_reset;
    gpu->status = psyz_status;
    gpu->sync = psyz_sync;
}

// these are not yet decompiled
int _addque2(int (*exec)(u_long p1, u_long p2), u_long p1, int len, u_long p2) {
    return 0;
}
int _exeque() { return 0; }
int get_alarm(void) { return 0; }
