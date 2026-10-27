#include <psyz.h>
#include <libgpu.h>
#include <psyz/gte.h>
#include "../../../decomp/src/libgpu/libgpu_private.h"
#include <psyz/log.h>
#include "../draw.h"
#include "../internal.h"
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

static void GPU_clear_cache() { NOT_IMPLEMENTED; }

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
        int sj = dst.y > rect.y ? h - 1 - j : j; // overlapping rows
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

int Psyz_GpuSetHorizontalGrid(
    unsigned int source_width, unsigned int target_width) {
    return Draw_SetHorizontalGrid(source_width, target_width);
}

static void DispatchPackets(u_long* buf, int len) {
    RECT rect;
    unsigned int x, y;
    for (int i = 0; i < len; i++) {
        u_long op = buf[i];
        int code = (int)(buf[i] >> 24) & 0xFF;
        // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gpu-render-polygon-commands
        switch (code) {
        case 0x00:
            // empty?!
            break;
        case 0x01: // clear cache
            GPU_clear_cache();
            break;
        case 0x02: // frame buffer rectangle draw
            rect.x = (short)(buf[i + 1] & 0xFFFF);
            rect.y = (short)((buf[i + 1] >> 16) & 0xFFFF);
            rect.w = (short)(buf[i + 2] & 0xFFFF);
            rect.h = (short)((buf[i + 2] >> 16) & 0xFFFF);
            VramCopyFill(rect, (u_char)(op & 0xFF), (u_char)((op >> 8) & 0xFF),
                         (u_char)((op >> 16) & 0xFF));
            Draw_ClearImage(
                &rect, (u_char)(op & 0xFF), (u_char)((op >> 8) & 0xFF),
                (u_char)((op >> 16) & 0xFF));
            i += 2;
            break;
        case 0x80: // move image
            rect.x = (short)(buf[i + 1] & 0xFFFF);
            rect.y = (short)((buf[i + 1] >> 16) & 0xFFFF);
            rect.w = (short)(buf[i + 3] & 0xFFFF);
            rect.h = (short)((buf[i + 3] >> 16) & 0xFFFF);
            x = (short)(buf[i + 2] & 0xFFFF);
            y = (short)((buf[i + 2] >> 16) & 0xFFFF);
            VramCopyMove(rect, (int)(short)x, (int)(short)y);
            Draw_MoveImage(&rect, x, y);
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
    }
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
    Draw_ExequeSync();
    queue_len = 0;
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
                    Psyz_GteCopyQueuedPgxpSxy(
                        &prim_data[i], &queue_buf[queue_len + i], prim_data[i]);
                }
            } else if (env->len > 0) {
                memcpy(queue_buf + queue_len, env->code,
                       env->len * sizeof(u_long));
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

static int GPU_DataWrite(RECT* rect, u_long* data) {
    Psyz_GpuExeque();
    VramCopyLoad(rect, data);
    Draw_LoadImage(rect, data);
    return 0;
}
static int GPU_DataRead(RECT* rect, u_long* data) {
    Psyz_GpuExeque();
    Draw_StoreImage(rect, data);
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
            Psyz_GteCopyQueuedPgxpSxy(&data[i], &queue_buf[i], data[i]);
        }
    } else {
        memcpy(queue_buf, data, (size_t)n * sizeof(u_long));
        for (int i = 0; i < n; ++i)
            Psyz_GteCopyQueuedPgxpSxy(
                (u_long*)data + i, &queue_buf[i], (unsigned)queue_buf[i]);
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

void Psyz_GpuDisplayCommand(unsigned int cmd) {
    unsigned char op = (cmd >> 24) & 0x3F;
    switch (op) {
    case 0:
        Draw_Reset();
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
        Draw_DisplayArea(cmd & 0x3FF, (cmd >> 10) & 0x3FF);
        break;
    case 6:
        Draw_DisplayHorizontalRange(cmd & 0xFFF, (cmd >> 12) & 0xFFF);
        break;
    case 7:
        Draw_DisplayVerticalRange(cmd & 0x3FF, (cmd >> 10) & 0x3FF);
        break;
    case 8:
        cmd &= 0xFFFFFF;
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
