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

// _card_info finds a card or times out; _card_load reads its directory and
// reports an unformatted card as a new one (EvSpNEW).
static long card_op(unsigned int sw_desc, long chan, int load) {
    if (!validate_chan(chan)) {
        return 0;
    }
    last_chan = chan;
    int state = (chan & 15) ? PSYZ_CARD_NONE : Psyz_CardSlotState((int)(chan >> 4));
    unsigned int spec = state == PSYZ_CARD_NONE ? EvSpTIMOUT
                        : load && state == PSYZ_CARD_UNFORMATTED ? EvSpNEW
                                                                 : EvSpIOE;
    if (sw_desc) {
        Psyz_KernelRaise(sw_desc, spec);
    }
    Psyz_KernelRaise(HwCARD, spec);
    return 1;
}

long _card_info(long chan) { return card_op(SwCARD, chan, 0); }

long _card_load(long chan) { return card_op(SwCARD, chan, 1); }

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

long _card_status(long drv) {
    (void)drv;
    return 1;
}

long _card_wait(long drv) {
    (void)drv;
    return 1;
}

unsigned long _card_chan(void) { return (unsigned long)last_chan; }

void InitCARD2(long val) { (void)val; }

long StartCARD2(void) { return 1; }

long StopCARD2(void) { return 1; }

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
    return card_op(0, chan, 0);
}

long _card_read(long chan, long block, unsigned char* buf) {
    LOG_ONCE("raw sectors are not emulated, only the completion event");
    (void)block;
    (void)buf;
    return card_op(0, chan, 0);
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
