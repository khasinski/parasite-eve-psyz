#include <psyz.h>
#include <libetc.h>

#include "libapi.h"
#include "../internal.h"

#include <psyz/log.h>

/* BIOS A(2Fh)/A(30h): unsigned 32-bit state, 15-bit result.
 * https://psx-spx.consoledev.net/ps1/kernelbios/misc-functions/
 * Before any srand the state is the word the kernel leaves at 0x80009010:
 * 0x24040001 with the SCPH-1001 BIOS (as on DuckStation), so a game that
 * never seeds draws the same numbers as there. */
static uint32_t biosRandomState = UINT32_C(0x24040001);
/* The ROM routines with the A0 stub, in system clocks (DuckStation). */
enum { BIOS_RAND_CYCLES = 433, BIOS_SRAND_CYCLES = 119 };
int Psyz_Rand(void) {
    biosRandomState = biosRandomState * UINT32_C(0x41c64e6d) + UINT32_C(0x3039);
    Psyz_SpendCycles(BIOS_RAND_CYCLES);
    return (int)((biosRandomState >> 16) & 0x7fff);
}
void Psyz_Srand(unsigned int seed) {
    biosRandomState = (uint32_t)seed;
    Psyz_SpendCycles(BIOS_SRAND_CYCLES);
}
unsigned int Psyz_RandState(void) { return biosRandomState; }

void MyPadInit(int mode);
void PadInit(int mode) { MyPadInit(mode); }

static unsigned UnpackPadBits(const char* buf) {
    if ((unsigned char)buf[0] != 0) {
        return 0; // port disconnected or frame not valid
    }
    switch ((unsigned char)buf[1]) {
    case PSYZ_CTRL_DIGITAL_PAD:
    case PSYZ_CTRL_ANALOG_STICK:
    case PSYZ_CTRL_ANALOG_PAD:
        break;
    default:
        return 0; // not a pad-like device, switches are not in bytes 2-3
    }
    unsigned int buttons0 = (unsigned char)buf[2];
    unsigned int buttons1 = (unsigned char)buf[3];
    unsigned int padq = (buttons0 << 8) | buttons1;
    return (~padq) & 0xFFFF;
}

unsigned int PadRead(int id) {
    char p0[PSYZ_PAD_BUF_LEN], p1[PSYZ_PAD_BUF_LEN];
    Psyz_PadsGet(0, p0, sizeof(p0));
    Psyz_PadsGet(1, p1, sizeof(p1));
    unsigned r = UnpackPadBits(p0) | (UnpackPadBits(p1) << 16);

    // PS1 gamepad cannot have opposite D-pad pressed at the same time
    // solves a bug on Castlevania SOTN when pressing both Left and Right
    // where its behavior is undefined.
    if (r & PADLleft && r & PADLright) {
        r &= ~(PADLleft | PADLright);
    }
    if (r & PADLup && r & PADLdown) {
        r &= ~(PADLup | PADLdown);
    }

    return r;
}

void PadStop(void) { NOT_IMPLEMENTED; }

static long video_mode = 0;
long GetVideoMode() { return video_mode; }
long SetVideoMode(long mode) {
    long prev = video_mode;
    video_mode = mode;
    Psyz_GpuConsoleVideoMode(mode == MODE_PAL);
    return prev;
}

static void (*dma_callbacks[7])(void);
static unsigned dma_pending;
static int dma_dispatching;

void* DMACallback(int dma, void (*func)()) {
    if (dma < 0 || dma >= 7)
        return NULL;
    void (*previous)(void) = dma_callbacks[dma];
    dma_callbacks[dma] = func;
    return (void*)previous;
}

/* Transfers finish synchronously on PC. A callback may start another one;
 * dispatch its completion after the current callback returns, as DMA would. */
void Psyz_DmaComplete(unsigned channel) {
    if (channel >= 7)
        return;
    dma_pending |= 1u << channel;
    if (dma_dispatching)
        return;
    dma_dispatching = 1;
    while (dma_pending) {
        unsigned next = 0;
        while (!(dma_pending & (1u << next)))
            ++next;
        dma_pending &= ~(1u << next);
        void (*callback)(void) = dma_callbacks[next];
        if (callback)
            callback();
    }
    dma_dispatching = 0;
}

int InitTAP(char* bufA, long lenA, char* bufB, long lenB) {
    LOG_ONCE("forwarding to InitPAD");
    return InitPAD(bufA, lenA, bufB, lenB);
}

void StartTAP(void) {
    LOG_ONCE("forwarding to StartPAD");
    StartPAD();
}

/* PSY-Q 4.2 direct-connection pad library (libpad). The receive buffers are
 * the InitPAD ones, refreshed every VSync with the PS1 frame for the kind
 * Psyz_PadsSetKind selects. A PSYZ_CTRL_ANALOG_PAD port is a DUAL SHOCK:
 * mode table {digital 0x4, analog 0x7}, two actuators, ready (stable) state.
 * Every other pad-like kind is a controller without vibration. */
enum {
    PAD_STATE_DISCON = 0,
    PAD_STATE_FIND_CTP1 = 2,
    PAD_STATE_STABLE = 6,
};

static struct {
    int dualshock;          // port reached the stable DUAL SHOCK state
    PsyzControllerKind cur; // mode selected on the DUAL SHOCK
} pad_direct[2];

static int PadDirectPort(int port) {
    if (port & ~0x10) {
        return -1; // Multi Tap channels B-D are never connected
    }
    return port >> 4;
}

static int PadDirectState(int port) {
    int p = PadDirectPort(port);
    if (p < 0) {
        return PAD_STATE_DISCON;
    }
    char frame[PSYZ_PAD_BUF_LEN];
    Psyz_PadsGet(p, frame, sizeof(frame));
    unsigned char kind = (unsigned char)frame[1];
    if (frame[0] != 0 || kind == PSYZ_CTRL_DISCONNECTED) {
        pad_direct[p].dualshock = 0;
        return PAD_STATE_DISCON;
    }
    if (pad_direct[p].dualshock &&
        (kind == PSYZ_CTRL_DIGITAL_PAD || kind == PSYZ_CTRL_ANALOG_PAD)) {
        return PAD_STATE_STABLE; // DUAL SHOCK, possibly in digital mode
    }
    if (kind == PSYZ_CTRL_ANALOG_PAD) {
        pad_direct[p].dualshock = 1;
        pad_direct[p].cur = PSYZ_CTRL_ANALOG_PAD;
        return PAD_STATE_STABLE;
    }
    pad_direct[p].dualshock = 0;
    return PAD_STATE_FIND_CTP1;
}

/* PSY-Q libpad clears its driver state (a 0x1E0-byte BIOS bzero) and sets
 * up the exchange: 73.3k system clocks, PadStartCom 1.6k (DuckStation). */
enum { PAD_INIT_DIRECT_CYCLES = 73316, PAD_START_COM_CYCLES = 1589 };

void PadInitDirect(u_char* pad1, u_char* pad2) {
    Psyz_SpendCycles(PAD_INIT_DIRECT_CYCLES);
    memset(pad_direct, 0, sizeof(pad_direct));
    InitPAD((char*)pad1, PSYZ_PAD_BUF_LEN, (char*)pad2, PSYZ_PAD_BUF_LEN);
}

static int pad_driver_running;
int Psyz_PadDriverRunning(void) { return pad_driver_running; }

void PadStartCom(void) {
    // receive buffers are already refreshed on every VSync
    Psyz_SpendCycles(PAD_START_COM_CYCLES);
    pad_driver_running = 1;
}

int PadGetState(int port) { return PadDirectState(port); }

int PadInfoMode(int port, int term, int offs) {
    int state = PadDirectState(port);
    if (state == PAD_STATE_DISCON) {
        return 0;
    }
    int p = PadDirectPort(port);
    char frame[PSYZ_PAD_BUF_LEN];
    Psyz_PadsGet(p, frame, sizeof(frame));
    int cur_id = ((unsigned char)frame[1] >> 4) & 0xF;
    if (state != PAD_STATE_STABLE) {
        return term == 1 ? cur_id : 0; // InfoModeCurID only
    }
    switch (term) {
    case 1: // InfoModeCurID
        return cur_id;
    case 2: // InfoModeCurExID: DUAL SHOCK reports controller type 1
        return 0x100 | cur_id;
    case 3: // InfoModeCurExOffs
        return pad_direct[p].cur == PSYZ_CTRL_ANALOG_PAD;
    case 4: // InfoModeIdTable
        if (offs == -1)
            return 2;
        if (offs == 0)
            return PSYZ_CTRL_DIGITAL_PAD >> 4;
        if (offs == 1)
            return PSYZ_CTRL_ANALOG_PAD >> 4;
        return 0;
    default:
        return 0;
    }
}

int PadInfoAct(int port, int actno, int term) {
    // DUAL SHOCK: 0 = small motor (on/off), 1 = large motor (variable)
    static const unsigned char act_info[2][4] = {
        {1, 1, 1, 10},
        {1, 2, 1, 20},
    };
    if (PadDirectState(port) != PAD_STATE_STABLE) {
        return 0;
    }
    if (actno == -1) {
        return LEN(act_info);
    }
    if (actno < 0 || actno >= LEN(act_info) || term < 1 || term > 4) {
        return 0;
    }
    return act_info[actno][term - 1];
}

int PadInfoComb(int port, int listno, int offs) {
    (void)port;
    (void)listno;
    (void)offs;
    return 0; // DUAL SHOCK motors have no combination restrictions
}

void PadSetAct(int port, u_char* data, int len) {
    // the host has no force feedback output; the buffer is not transmitted
    (void)port;
    (void)data;
    (void)len;
}

int PadSetActAlign(int port, char* data) {
    (void)data;
    return PadDirectState(port) == PAD_STATE_STABLE;
}

int PadSetMainMode(int port, int offs, int lock) {
    (void)lock; // the host has no mode selector button to lock
    if (PadDirectState(port) != PAD_STATE_STABLE || offs < 0 || offs > 1) {
        return 0;
    }
    int p = PadDirectPort(port);
    PsyzControllerKind kind =
        offs ? PSYZ_CTRL_ANALOG_PAD : PSYZ_CTRL_DIGITAL_PAD;
    pad_direct[p].cur = kind;
    Psyz_PadsSetKind(p, 0, kind);
    return 1;
}

void PadInitMtap(u_char* pad1, u_char* pad2) {
    // from PSY-Q 4.2
    NOT_IMPLEMENTED;
}
