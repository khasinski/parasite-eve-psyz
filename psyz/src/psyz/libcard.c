#include <stdio.h>
#include <psyz.h>
#include <libapi.h>
#include <psyz/log.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <libetc.h>
#include <psyz/system.h>
#include "../internal.h"

// 1:valid, 0:invalid
static inline int validate_chan(long chan) {
    unsigned int cardNo = (unsigned int)(chan & 15);
    unsigned int portNo = (unsigned int)(chan >> 4);
    if (cardNo >= 4) { // validate multi-tap card support
        return 0;
    }
    if (portNo >= 2) { // PS1 has only two memcard ports
        return 0;
    }
    return 1;
}

long _card_auto(long val) {
    NOT_IMPLEMENTED;
    return val;
}

static long last_chan;

/* Cards live as host directories (bu00, bu10) and are plugged in and
 * formatted unless the host says otherwise: PSYZ_CARD_UNPLUG and
 * PSYZ_CARD_UNFORMATTED list slots (1, 2) that are empty or hold an
 * unformatted card, e.g. PSYZ_CARD_UNPLUG=2; Psyz_CardSetSlotState does the
 * same at run time. Formatting a card makes it formatted. There is no
 * multi-tap, so its extra slots time out like an empty port. */
static int card_slot_state[2] = {-1, -1};

static int card_env_lists(const char* name, int slot) {
    const char* list = getenv(name);
    if (!list) {
        return 0;
    }
    for (; *list; list++) {
        if (*list == '0' + slot) {
            return 1;
        }
    }
    return 0;
}

int Psyz_CardSlotState(int port) {
    if (port < 0 || port > 1) {
        return PSYZ_CARD_NONE;
    }
    if (card_slot_state[port] < 0) {
        card_slot_state[port] =
            card_env_lists("PSYZ_CARD_UNPLUG", port + 1) ? PSYZ_CARD_NONE
            : card_env_lists("PSYZ_CARD_UNFORMATTED", port + 1)
                ? PSYZ_CARD_UNFORMATTED
                : PSYZ_CARD_FORMATTED;
    }
    return card_slot_state[port];
}

void Psyz_CardSetSlotState(int port, int state) {
    if (port >= 0 && port <= 1) {
        card_slot_state[port] = state;
    }
}

static int card_is_instant(void);
static uint64_t card_fields_us(unsigned fields);

/* BIOS card driver timing (measured on DuckStation with the SCPH-1001 BIOS
 * and Ridge Racer Type 4, PAL). After StartCARD the driver's vblank handler
 * alternates between the two ports, one per vblank, and starts the transfer
 * a port has pending on that port's vblank: a sector read or write, which
 * the card answers within the field, so back-to-back requests on one port
 * take two fields each. A port without a card leaves the transfer
 * unanswered: the next vblank times it out (EvSpTIMOUT) instead of switching
 * ports, which shifts the alternation by one field.
 *
 * Requests count transfers: _card_info, _card_read, _card_write and
 * _card_clear are one; _card_load reads sector 0 and, when it holds a
 * directory, the directory and broken-sector frames 1..35 (36), and
 * _bu_init also writes sector 0x3F (the new-card flag) after sector 0 (37).
 * A finished request raises its spec on SwCARD and HwCARD at that vblank
 * (before the game's vblank callbacks, as the BIOS handler runs first);
 * _card_load parses the directory in the handler and raises it at the
 * next vblank, which therefore does not switch ports. _bu_init waits for
 * both ports in turn and raises nothing: a blank card in port 1 and an
 * empty port 2 take 76 fields, an unformatted card in port 1 four and two
 * empty ports five. Psyz_CardSetInstantTiming(1) / PSYZ_CARD_TIMING=instant
 * completes every request at once. */
enum { CARD_LOAD_TRANSFERS = 36, CARD_BU_INIT_TRANSFERS = 37 };

typedef struct {
    long chan;
    int transfers; // still to make; 0: no request
    int raise;     // raise the result on SwCARD and HwCARD when done
    int load;      // an unformatted card answers EvSpNEW
    int parse;     // the directory is parsed a vblank after the last read
} CardRequest;

static struct {
    int running;      // StartCARD .. StopCARD
    int port;         // the port served at the last vblank
    int timeout_port; // an unanswered transfer, timed out at the next vblank
    int parse_port;   // a directory read, finished at the next vblank
    int raw_port;     // a raw command waiting for its port (-1: none)
    int raw_state;    // 0 none, 1 waiting, 2 transferred, 3 answered
    uint64_t last_vblank_us;
    CardRequest req[2];
} card_drv = {0, 0, -1, -1, -1, 0, 0, {{0}}};

// PSYZ_CARD_TRACE=1 logs the driver's work at each vblank.
static int card_trace(void) {
    static int trace = -1;
    if (trace < 0) {
        const char* env = getenv("PSYZ_CARD_TRACE");
        trace = env && *env && *env != '0';
    }
    return trace;
}

static int card_chan_state(long chan) {
    return (chan & 15) ? PSYZ_CARD_NONE : Psyz_CardSlotState((int)(chan >> 4));
}

static void card_raise(unsigned spec) {
    Psyz_KernelRaise(SwCARD, spec);
    Psyz_KernelRaise(HwCARD, spec);
}

static unsigned card_result(const CardRequest* r) {
    int state = card_chan_state(r->chan);
    return state == PSYZ_CARD_NONE                    ? EvSpTIMOUT
           : r->load && state == PSYZ_CARD_UNFORMATTED ? EvSpNEW
                                                       : EvSpIOE;
}

static void card_finish(int port, unsigned spec) {
    CardRequest* r = &card_drv.req[port];
    int raise = r->raise;
    r->transfers = 0;
    if (raise) {
        card_raise(spec);
    }
}

// The BIOS card handler's work at one vblank.
void Psyz_CardVBlank(void) {
    if (!card_drv.running) {
        return;
    }
    card_drv.last_vblank_us = Psyz_KernelNowUs();
    if (card_drv.raw_state == 2) {
        card_drv.raw_state = 3;
    }
    if (card_drv.parse_port >= 0) {
        int port = card_drv.parse_port;
        card_drv.parse_port = -1;
        card_finish(port, card_result(&card_drv.req[port]));
        return;
    }
    if (card_drv.timeout_port >= 0) {
        int port = card_drv.timeout_port;
        card_drv.timeout_port = -1;
        if (card_trace()) {
            fprintf(stderr, "card: vblank port %d times out\n", port);
        }
        card_finish(port, EvSpTIMOUT);
        return;
    }
    card_drv.port ^= 1;
    int port = card_drv.port;
    if (card_trace()) {
        fprintf(stderr, "card: vblank port %d transfers %d raw %d\n", port,
                card_drv.req[port].transfers, card_drv.raw_state);
    }
    if (card_drv.raw_state == 1 && card_drv.raw_port == port) {
        card_drv.raw_state = 2;
        return;
    }
    CardRequest* r = &card_drv.req[port];
    if (!r->transfers) {
        return;
    }
    int state = card_chan_state(r->chan);
    if (state == PSYZ_CARD_NONE) {
        card_drv.timeout_port = port;
        return;
    }
    if (r->load && state == PSYZ_CARD_UNFORMATTED) {
        r->transfers = 1; // sector 0 holds no directory
        r->parse = 0;
    }
    if (--r->transfers > 0) {
        return;
    }
    if (r->parse) {
        r->transfers = 1; // busy until the directory is parsed
        card_drv.parse_port = port;
    } else {
        card_finish(port, card_result(r));
    }
}

/* A PS1 program spinning on the card driver: spends a scanline of PS1 time.
 * Blanks normally come from that time (the offline clock); a clock that
 * does not raise them in a wait (real time, or no audio clock yet) gets the
 * driver's vblank after a field. */
static void card_spin(void) {
    uint64_t before = Psyz_KernelNowUs();
    Psyz_SpendTime(64000);
    uint64_t now = Psyz_KernelNowUs();
    if (now == before || now - card_drv.last_vblank_us > card_fields_us(1)) {
        Psyz_CardVBlank();
        card_drv.last_vblank_us = now;
    }
}

static long card_request(long chan, int transfers, int raise, int load, int parse) {
    if (!validate_chan(chan)) {
        return 0;
    }
    last_chan = chan;
    if (card_is_instant()) {
        CardRequest r = {chan, 0, raise, load, 0};
        if (raise) {
            card_raise(card_result(&r));
        }
        return 1;
    }
    CardRequest* r = &card_drv.req[chan >> 4];
    if (r->transfers) {
        return 0; // the port is busy
    }
    r->chan = chan;
    r->transfers = transfers;
    r->raise = raise;
    r->load = load;
    r->parse = parse;
    return 1;
}

static void card_wait(int port) {
    while (card_drv.running && card_drv.req[port].transfers) {
        card_spin();
    }
}

long _card_info(long chan) { return card_request(chan, 1, 1, 0, 0); }

long _card_load(long chan) {
    return card_request(chan, CARD_LOAD_TRANSFERS, 1, 1, 1);
}

void _bu_init(void) {
#ifdef PE_PS5
    char path[4096];
    Psyz_AdjustPath(path, "bu00:*", sizeof(path));
    Psyz_AdjustPath(path, "bu10:*", sizeof(path));
#elif defined(_MSC_VER)
    NOT_IMPLEMENTED;
#elif __MINGW32__
    mkdir("bu00");
    mkdir("bu10");
#else
    mkdir("bu00", 0755);
    mkdir("bu10", 0755);
#endif
    if (card_is_instant() || !card_drv.running) {
        return;
    }
    for (int port = 0; port < 2; port++) {
        if (card_request((long)port << 4, CARD_BU_INIT_TRANSFERS, 0, 1, 0)) {
            card_wait(port);
        }
    }
}

void Psyz_CardSyncTransfer(long chan) {
    if (card_request(chan, 1, 0, 0, 0) && !card_is_instant()) {
        card_wait((int)(chan >> 4));
    }
}

int Psyz_CardRawBegin(long chan) {
    if (!validate_chan(chan) || card_drv.raw_state) {
        return 0;
    }
    card_drv.raw_port = (int)(chan >> 4);
    card_drv.raw_state = card_is_instant() || !card_drv.running ? 3 : 1;
    return 1;
}

int Psyz_CardRawSync(int wait) {
    if (!card_drv.raw_state) {
        return -1;
    }
    while (wait && card_drv.raw_state != 3) {
        card_spin();
    }
    if (card_drv.raw_state != 3) {
        return 0;
    }
    card_drv.raw_state = 0;
    card_drv.raw_port = -1;
    return 1;
}

void _new_card(void) {}

static void card_spend_fields(unsigned fields);
enum { CARD_FORMAT_FIELDS = 149 };

// A card is a host directory; formatting empties it, taking the BIOS's time
// to write the directory (149 fields on DuckStation, PAL). There is no
// multi-tap.
long _card_format(long chan) {
    char fs[] = "bu00:";
    if (!validate_chan(chan) || (chan & 15)) {
        return 0;
    }
    if (Psyz_CardSlotState((int)(chan >> 4)) == PSYZ_CARD_NONE) {
        return 0;
    }
    fs[2] = (char)('0' + (chan >> 4));
    card_spend_fields(CARD_FORMAT_FIELDS);
    if (!format(fs)) {
        return 0;
    }
    card_slot_state[chan >> 4] = PSYZ_CARD_FORMATTED;
    return 1;
}

// 1 once the port's request is done; a caller polling it spins on the
// driver.
long _card_status(long drv) {
    if (drv < 0 || drv > 1 || !card_drv.req[drv].transfers) {
        return 1;
    }
    card_spin();
    return card_drv.req[drv].transfers ? 0 : 1;
}

long _card_wait(long drv) {
    if (drv >= 0 && drv <= 1) {
        card_wait((int)drv);
    }
    return 1;
}

unsigned long _card_chan(void) { return (unsigned long)last_chan; }

static void card_driver_reset(int running) {
    memset(card_drv.req, 0, sizeof(card_drv.req));
    card_drv.running = running;
    card_drv.port = 0;
    card_drv.timeout_port = -1;
    card_drv.parse_port = -1;
    card_drv.raw_port = -1;
    card_drv.raw_state = 0;
    card_drv.last_vblank_us = Psyz_KernelNowUs();
}

void InitCARD2(long val) {
    (void)val;
    card_driver_reset(0);
}

long StartCARD2(void) {
    card_driver_reset(1);
    return 1;
}

long StopCARD2(void) {
    card_driver_reset(0);
    return 1;
}

void _ExitCard(void) {}

static void _bzero(unsigned char* p, int n) { memset(p, 0, n); }

long _card_sector_write(long chan, long block, unsigned char* buf) {
    unsigned char sp10[0x80];
    u8 checksum;
    s32 retries;
    s32 i;
    u8* var_v1;
    u8* var_v1_2;

    retries = 0;
    var_v1 = buf;
    checksum = 0;
    for (i = 0; i < 0x7F; i++) {
        checksum ^= *var_v1++;
    }
    *var_v1 = checksum;
    while (1) {
        if (retries < 8) {
            _new_card();
            if (_card_write(chan, block, buf) == 1) {
                do {
                } while (!(_card_status(chan >> 4) & 1));
                var_v1_2 = sp10;
                _bzero(var_v1_2, sizeof(sp10));
                _new_card();
                if (_card_read(chan, block, var_v1_2) != 1) {
                    ERRORF("card read error\n");
                } else {
                    do {
                    } while (!(_card_status(chan >> 4) & 1));
                }
                var_v1 = sp10;
                checksum = 0;
                for (i = 0; i < 0x7F; i++) {
                    checksum ^= *var_v1++;
                }
                if (buf[0x7F] == checksum) {
                    return 1;
                }
                retries++;
            } else {
                return 0;
            }
        } else {
            break;
        }
    }
    return 0;
}

long _card_write(long chan, long block, unsigned char* buf) {
    LOG_ONCE("raw sectors are not emulated, only the completion event");
    (void)block;
    (void)buf;
    return card_request(chan, 1, 1, 0, 0);
}

long _card_read(long chan, long block, unsigned char* buf) {
    LOG_ONCE("raw sectors are not emulated, only the completion event");
    (void)block;
    (void)buf;
    return card_request(chan, 1, 1, 0, 0);
}

/* Memory card file timing ("bu" device). The BIOS moves one 128-byte frame
 * of a file about every two fields (the card driver steps its transfers on
 * the vblank interrupt), so an 8 KiB block takes 128 fields plus a few of
 * setup; an FASYNC read or write returns at once and raises SwCARD/EvSpIOE
 * when its last frame is done, a synchronous one returns then. Creating a
 * file first writes its directory frames. Measured on DuckStation with
 * Ridge Racer Type 4 (PAL): a 1-block save or load waits 133..134 fields
 * for its event and the file creation blocks 8 fields.
 * Psyz_CardSetInstantTiming(1) or PSYZ_CARD_TIMING=instant completes card
 * I/O at once. */
enum { CARD_FIELDS_PER_FRAME = 2, CARD_SETUP_FIELDS = 5, CARD_CREATE_FIELDS = 8 };

static int card_instant = -1;

void Psyz_CardSetInstantTiming(int instant) { card_instant = instant != 0; }

static int card_is_instant(void) {
    if (card_instant < 0) {
        const char* timing = getenv("PSYZ_CARD_TIMING");
        card_instant = timing && !strcmp(timing, "instant");
    }
    return card_instant;
}

static uint64_t card_fields_us(unsigned fields) {
    /* PS1 field: 3406 x 314 (PAL) or 3413 x 263 (NTSC) GPU clocks at
     * 53.693175 MHz. */
    uint64_t clocks = GetVideoMode() == MODE_PAL ? 3406ull * 314 : 3413ull * 263;
    return (fields * clocks * 1000 + 26846) / 53693;
}

static unsigned card_transfer_fields(long bytes) {
    long frames = (bytes + 127) / 128;
    return (unsigned)(CARD_SETUP_FIELDS + frames * CARD_FIELDS_PER_FRAME);
}

static void card_spend_fields(unsigned fields) {
    if (card_is_instant()) {
        return;
    }
    for (; fields; fields--) { // one field at a time keeps ns within 32 bits
        Psyz_SpendTime((unsigned)(card_fields_us(1) * 1000));
    }
}

static void card_async_done(void) { Psyz_KernelRaise(SwCARD, EvSpIOE); }

void Psyz_CardFileTransfer(long bytes, int async) {
    if (card_is_instant()) {
        if (async) {
            card_async_done();
        }
        return;
    }
    uint64_t us = card_fields_us(card_transfer_fields(bytes));
    if (async) {
        Psyz_KernelPostAt(Psyz_KernelNowUs() + us, card_async_done);
    } else {
        card_spend_fields(card_transfer_fields(bytes));
    }
}

void Psyz_CardFileCreate(void) { card_spend_fields(CARD_CREATE_FIELDS); }
