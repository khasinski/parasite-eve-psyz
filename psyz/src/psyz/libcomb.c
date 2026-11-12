#include <psyz.h>
#include <libcomb.h>
#include <psyz/log.h>
#include <psyz/system.h>

// The host has no link cable: the serial controller is idle with nothing on
// the other end, so DSR and CTS stay off and no data is ever received.
static struct {
    u_long control;
    u_long mode;
    u_long bps;
    u_long packet_size;
    u_long wait_cb;
} comb = {0, COMB_MODE_CHAR_8 | COMB_MODE_STOP_1, 9600, 1, 0};

/* Installing the "sio" device takes the BIOS's AddDevice and the driver's
 * setup: 66.1k system clocks (DuckStation, SCPH-1001 BIOS; Ridge Racer
 * Type 4's libsio AddSIO). */
enum { ADD_SIO_DEVICE_CYCLES = 66100 };

void AddCOMB(void) { Psyz_SpendCycles(ADD_SIO_DEVICE_CYCLES); }

void DelCOMB(void) {}

void ChangeClearSIO(long val) { (void)val; }

long _comb_control(u_long cmd, u_long arg, u_long param) {
    switch (cmd) {
    case 0:
        switch (arg) {
        case 0:
            return COMB_STAT_TX_RDY | COMB_STAT_TX_EMPTY;
        case 1:
            return (long)comb.control;
        case 2:
            return (long)comb.mode;
        case 3:
            return (long)comb.bps;
        case 4:
            return (long)comb.packet_size;
        default: // 5: no bytes remain, 6: no request is pending
            return 0;
        }
    case 1:
        switch (arg) {
        case 1:
            comb.control = param;
            break;
        case 2:
            comb.mode = param;
            break;
        case 3:
            comb.bps = param;
            break;
        case 4:
            comb.packet_size = param;
            break;
        }
        return 0;
    case 2: // reset, clear errors, cancel requests: nothing is in flight
        return 0;
    case 3:
        if (arg == 0) {
            comb.control = param ? comb.control | COMB_CTRL_RTS
                                 : comb.control & ~(u_long)COMB_CTRL_RTS;
            return 0;
        }
        return 0; // CTS is never asserted without a peer
    case 4:
        comb.wait_cb = param;
        return 0;
    default:
        return 0;
    }
}
