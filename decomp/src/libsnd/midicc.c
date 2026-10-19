#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midicc", _SsSetControlChange);
#else
/* Reads the controller value and dispatches it through SsFCALL.control.
 * Every handler reads the following delta time. */
void _SsSetControlChange(short sep_access_num, short seq_num, unsigned char cc) {
    struct SeqStruct* score = &_ss_score[sep_access_num][seq_num];
    unsigned char value = *score->seq_ptr++;

    switch (cc) {
    case 0:
        score->unk47 = value; /* bank select value, see _SsContBankChange */
        ((SsFnEvent2)SsFCALL.control[CC_BANKCHANGE])(sep_access_num, seq_num);
        break;
    case 6:
        ((SsFnEvent)SsFCALL.control[CC_DATAENTRY])(sep_access_num, seq_num, value);
        break;
    case 7:
        ((SsFnEvent)SsFCALL.control[CC_MAINVOL])(sep_access_num, seq_num, value);
        break;
    case 10:
        ((SsFnEvent)SsFCALL.control[CC_PANPOT])(sep_access_num, seq_num, value);
        break;
    case 11:
        ((SsFnEvent)SsFCALL.control[CC_EXPRESSION])(sep_access_num, seq_num, value);
        break;
    case 64:
        ((SsFnEvent)SsFCALL.control[CC_DAMPER])(sep_access_num, seq_num, value);
        break;
    case 91:
        ((SsFnEvent)SsFCALL.control[CC_EXTERNAL])(sep_access_num, seq_num, value);
        break;
    case 98:
        ((SsFnEvent)SsFCALL.control[CC_NRPN1])(sep_access_num, seq_num, value);
        break;
    case 99:
        ((SsFnEvent)SsFCALL.control[CC_NRPN2])(sep_access_num, seq_num, value);
        break;
    case 100:
        ((SsFnEvent)SsFCALL.control[CC_RPN1])(sep_access_num, seq_num, value);
        break;
    case 101:
        ((SsFnEvent)SsFCALL.control[CC_RPN2])(sep_access_num, seq_num, value);
        break;
    case 121:
        ((SsFnEvent2)SsFCALL.control[CC_RESETALL])(sep_access_num, seq_num);
        break;
    default:
        score->delta_value = _SsReadDeltaValue(sep_access_num, seq_num);
        break;
    }
}
#endif
