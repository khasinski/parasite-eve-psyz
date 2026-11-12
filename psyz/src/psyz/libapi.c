#include <psyz.h>
#include <psyz/log.h>
#include <kernel.h>
#include <libetc.h>
#include "libgpu.h"
#include "../draw.h"
#include "../internal.h"

#undef _get_errno // Windows: avoid conflicts
#undef undelete   // macOS: avoid conflicts

typedef struct {
    char frame[PSYZ_PAD_BUF_LEN];   // last frame published by the platform
    PsyzControllerKind channels[4]; // only channel 0 until multitap lands
} ControllerPort;

static ControllerPort ports[2] = {
    {{0},
     {
         PSYZ_CTRL_DIGITAL_PAD,
         PSYZ_CTRL_DISCONNECTED,
         PSYZ_CTRL_DISCONNECTED,
         PSYZ_CTRL_DISCONNECTED,
     }},
    {{0},
     {
         PSYZ_CTRL_DIGITAL_PAD,
         PSYZ_CTRL_DISCONNECTED,
         PSYZ_CTRL_DISCONNECTED,
         PSYZ_CTRL_DISCONNECTED,
     }},
};

static int pads_sampled = 0; // avoid more than one input polling per frame
static char* pad_buffers[2];
static int pad_buffer_lens[2];
static void ReadPadsOnVsync(void) {
    pads_sampled = 0;
    for (int p = 0; p < LEN(ports); p++) {
        if (pad_buffers[p]) {
            Psyz_PadsGet(p, pad_buffers[p], pad_buffer_lens[p]);
        }
    }
}

static PsyzVSyncCb g_PsyzVsyncCb = NULL;
PsyzVSyncCb Psyz_SetVSyncCb(PsyzVSyncCb cb) {
    PsyzVSyncCb prev = g_PsyzVsyncCb;
    g_PsyzVsyncCb = cb;
    return prev;
}

/* The vblank handler's PS1 time besides the kernel's: libetc's VSync
 * handler and callbacks dispatch, the BIOS card driver's turn (2.4k clocks
 * on DuckStation), and libpad's controller exchange once PadStartCom ran
 * (16.1k: its SIO busy waits for a digital pad in port 1, port 2 empty). */
enum { VBLANK_HANDLER_CYCLES = 2387, LIBPAD_VBLANK_CYCLES = 16084 };

static uint64_t last_blank_us; // emulated time of the latest blank

void Psyz_VSyncBlank(void) {
    last_blank_us = Psyz_KernelNowUs();
    Psyz_GpuCrtcBlank();
    Psyz_KernelInterruptCycles(
        VBLANK_HANDLER_CYCLES +
        (Psyz_PadDriverRunning() ? LIBPAD_VBLANK_CYCLES : 0));
    ReadPadsOnVsync();
    Psyz_CardVBlank();
    if (g_PsyzVsyncCb)
        g_PsyzVsyncCb();
    Psyz_KernelVBlank();
}

/* SDK VSync(n) waits for the interval since the previous blocking call,
 * then at least the next blank. Queries do not reset that interval. */
static unsigned int last_vsync_count;

/* In 480-line interlace libetc's VSync also waits, after the blank, until
 * GPUSTAT bit 31 differs from its value before the blank wait: at once when
 * the field read was odd, else at the start of the display range (about 48
 * lines into the next field for a PAL 256-line screen), so every other
 * VSync(0) returns late (DuckStation's frames alternate 266 and 362 lines);
 * a read in the blank of an overrun frame can wait a further field. VSync(1)
 * counts lines from that return, as the PS1's Hcount latch does (the
 * platform counts from the blank of its last VSync(0)). */
static unsigned int vsync_return_lines;

static unsigned LineNs(void) {
    return Psyz_GpuCrtcFieldLines() == 314 ? 64017 : 63563;
}

static unsigned LinesSinceBlank(void) {
    uint64_t now_us = Psyz_KernelNowUs();
    if (now_us <= last_blank_us) {
        return 0;
    }
    return (unsigned)((now_us - last_blank_us) * 1000 / LineNs());
}

static unsigned int WaitBlank(unsigned int now) {
    unsigned int before = now;
    Psyz_VideoVSync(0);
    now = (unsigned int)Psyz_VideoVSync(-1);
    unsigned int advanced = now - before;
    while (advanced--) {
        Psyz_VSyncBlank();
    }
    return now;
}

/* The lines after the blank until GPUSTAT bit 31 differs from `odd_read`;
 * whole fields it stays the same are waited here. */
static unsigned InterlaceWait(int odd_read, unsigned int* now) {
    for (int fields = 0; fields < 3; fields++) {
        if (Psyz_GpuCrtcOddLine(0) != odd_read) {
            return 0; // flipped at the blank
        }
        unsigned blank = Psyz_GpuCrtcBlankLines();
        if (Psyz_GpuCrtcOddLine(blank) != odd_read) {
            return blank;
        }
        Psyz_KernelTakeInterruptCycles(); // the poll loop runs meanwhile
        *now = WaitBlank(*now);
    }
    return 0;
}

int VSync(int mode) {
    Psyz_KernelPoll();
    if (mode < 0)
        return Psyz_VideoVSync(-1);
    if (mode == 1) {
        Psyz_KernelTakeInterruptCycles(); // a polling loop absorbs handlers
        return (unsigned short)(Psyz_VideoVSync(1) - vsync_return_lines);
    }

    int raw = Psyz_VideoVSync(1);
    int elapsed = (unsigned short)(raw - vsync_return_lines);
    unsigned int now = (unsigned int)Psyz_VideoVSync(-1);
    unsigned int target =
        mode > 1 ? last_vsync_count + (unsigned int)mode : now + 1;
    if ((int32_t)(target - now) < 1)
        target = now + 1;
    // GPUSTAT as libetc reads it: now, or at the blank before the target
    // offline clock only: real-time presentation paces the fields itself
    int interlace = Psyz_GpuCrtcInterlaced480() && Psyz_KernelOfflineClock();
    int odd_read = 0;
    if (interlace) {
        odd_read = Psyz_GpuCrtcOddLine(
            (int32_t)(target - 1 - now) > 0 ? 0 : LinesSinceBlank());
    }
    Psyz_KernelTakeInterruptCycles(); // the wait loop absorbs earlier handlers
    do {
        now = WaitBlank(now);
    } while ((int32_t)(now - target) < 0);
    unsigned wait_lines = interlace ? InterlaceWait(odd_read, &now) : 0;
    /* The loop sees the blank once the vblank handler is done: VSync returns
     * after it, or at the interlace field change if that is later. */
    uint64_t handler_ns = (uint64_t)Psyz_KernelTakeInterruptCycles() *
                          1000000000u / 33868800u;
    uint64_t wait_ns = (uint64_t)wait_lines * LineNs();
    if (handler_ns > wait_ns) {
        wait_ns = handler_ns;
    }
    if (wait_ns) {
        Psyz_SpendTime((unsigned)wait_ns);
    }
    vsync_return_lines = (unsigned)((wait_ns + LineNs() / 2) / LineNs());
    Psyz_CdServiceDrive();
    /* Keep periodic waits on their scheduled blank. Native presentation can
     * return just after it; using that late return as the next origin adds a
     * blank repeatedly and lets FMV pictures drift behind device audio.
     * A whole missed interval resynchronizes rather than replaying a stall. */
    last_vsync_count =
        mode > 1 && now - target < (unsigned int)mode ? target : now;
    return elapsed;
}

PsyzControllerKind Psyz_PadsSetKind(
    int port, int channel, PsyzControllerKind kind) {
    if (port < 0 || port >= LEN(ports)) {
        return PSYZ_CTRL_ERROR;
    }
    if (channel < 0 || channel >= LEN(ports->channels)) {
        return PSYZ_CTRL_ERROR;
    }
    if (kind == PSYZ_CTRL_QUERY_KIND) {
        return ports[port].channels[channel];
    }
    PsyzControllerKind prev = ports[port].channels[channel];
    ports[port].channels[channel] = kind;
    return prev;
}

void Psyz_PadsSet(int port, const char* src, int len) {
    if (port < 0 || port >= LEN(ports) || !src || len <= 0) {
        return;
    }
    if (len > PSYZ_PAD_BUF_LEN) {
        len = PSYZ_PAD_BUF_LEN;
    }
    memcpy(ports[port].frame, src, len);
    pads_sampled = 1;
}

void Psyz_PadsPoll(void); // implemented by the platform layer
void Psyz_PadsGet(int port, char* dst, int len) {
    if (port < 0 || port >= LEN(ports) || !dst || len <= 0) {
        return;
    }
    if (!pads_sampled) {
        pads_sampled = 1;
        Psyz_PadsPoll();
    }
    if (len > PSYZ_PAD_BUF_LEN) {
        len = PSYZ_PAD_BUF_LEN;
    }
    memcpy(dst, ports[port].frame, len);
}

int InitPAD(char* bufA, int lenA, char* bufB, int lenB) {
    PadInit(0);
    if (bufA) {
        memset(bufA, 0, lenA);
    } else {
        WARNF("bufA is NULL");
    }
    if (bufB) {
        memset(bufB, 0, lenB);
    } else {
        WARNF("bufB is NULL");
    }
    pad_buffers[0] = bufA;
    pad_buffer_lens[0] = lenA;
    pad_buffers[1] = bufB;
    pad_buffer_lens[1] = lenB;
    return 1;
}

long StartPAD(void) {
    NOT_IMPLEMENTED;
    return 1;
}

void StopPAD(void) { NOT_IMPLEMENTED; }

int PAD_init(int type, void* unused) {
    PadInit(0);
    return 1;
}

int PAD_dr(int port, char* dst) {
    if (port < 0 || port > 1 || !dst) {
        return 0;
    }
    Psyz_PadsGet(port, dst, PSYZ_PAD_BUF_LEN);
    return PSYZ_PAD_BUF_LEN;
}

void _96_remove(void) { NOT_IMPLEMENTED; }

long ReadInitPadFlag(void) {
    NOT_IMPLEMENTED;
    return 0;
}

void ChangeClearPAD(long val) { (void)val; }

void SystemError(char c, long n) {
    NOT_IMPLEMENTED;
    ERRORF("SystemError('%c', 0x%X)", c, n);
}

struct DIRENTRY* my_firstfile(char* dirPath, struct DIRENTRY* firstEntry);
struct DIRENTRY* firstfile(char* dirPath, struct DIRENTRY* firstEntry) {
    // The BIOS checks the card ("buPS:") by reading its sector 0 first.
    if (dirPath && dirPath[0] == 'b' && dirPath[1] == 'u' &&
        dirPath[2] >= '0' && dirPath[2] <= '1' && dirPath[3] >= '0' &&
        dirPath[3] <= '3') {
        Psyz_CardSyncTransfer(((dirPath[2] - '0') << 4) | (dirPath[3] - '0'));
    }
    return my_firstfile(dirPath, firstEntry);
}

struct DIRENTRY* my_nextfile(struct DIRENTRY* outEntry);
struct DIRENTRY* nextfile(struct DIRENTRY* outEntry) {
    return my_nextfile(outEntry);
}

long my_erase(char* path);
long erase(char* path) { return my_erase(path); }

long psyz_undelete(char* name) {
    NOT_IMPLEMENTED;
    return 0;
}

long my_format(char* fs);
long format(char* fs) { return my_format(fs); }

int psyz_get_errno(void) {
    NOT_IMPLEMENTED;
    return 0;
}

/* BIOS A(28h)/A(2Ah)/A(2Bh), with the time of the ROM byte loops (system.h). */
enum {
    BIOS_FILL_CYCLES = 328,
    BIOS_FILL_BYTE_CYCLES = 132,
    BIOS_COPY_CYCLES = 258,
    BIOS_COPY_BYTE_CYCLES = 204,
};

static void BiosByteLoopTime(unsigned base, unsigned per_byte, int len) {
    uint64_t cycles = base + (len > 0 ? (uint64_t)len * per_byte : 0);
    while (cycles > UINT32_MAX) {
        Psyz_SpendCycles(UINT32_MAX);
        cycles -= UINT32_MAX;
    }
    Psyz_SpendCycles((unsigned)cycles);
}

void Psyz_BiosBzero(void* dst, int len) {
    if (dst && len > 0) {
        memset(dst, 0, (size_t)len);
    }
    BiosByteLoopTime(BIOS_FILL_CYCLES, BIOS_FILL_BYTE_CYCLES, dst ? len : 0);
}

void* Psyz_BiosMemset(void* dst, int c, int len) {
    if (dst && len > 0) {
        memset(dst, c & 0xFF, (size_t)len);
    }
    BiosByteLoopTime(BIOS_FILL_CYCLES, BIOS_FILL_BYTE_CYCLES, dst ? len : 0);
    return dst;
}

void* Psyz_BiosMemcpy(void* dst, const void* src, int len) {
    if (dst && src && len > 0) {
        // the ROM copies forwards byte by byte, overlapping or not
        unsigned char* d = dst;
        const unsigned char* s = src;
        for (int i = 0; i < len; i++) {
            d[i] = s[i];
        }
    }
    BiosByteLoopTime(BIOS_COPY_CYCLES, BIOS_COPY_BYTE_CYCLES,
                     dst && src ? len : 0);
    return dst;
}
