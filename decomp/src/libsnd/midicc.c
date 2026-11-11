#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/midicc", _SsSetControlChange);
#else
typedef void (*SsControlFn)(short, short, unsigned char);

static void Control(int index, short seq_access_num, short seq_num, u8 value) {
    ((SsControlFn)SsFCALL.control[index])(seq_access_num, seq_num, value);
}

void _SsSetControlChange(
    short seq_access_num, short seq_num, unsigned char control) {
    struct SeqStruct* score = &_ss_score[seq_access_num][seq_num];
    u8 value = *score->seq_ptr++;

    switch (control) {
    case 0:
        score->vab_id = value;
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
        break;
    case 6:
        Control(CC_DATAENTRY, seq_access_num, seq_num, value);
        break;
    case 7:
        Control(CC_MAINVOL, seq_access_num, seq_num, value);
        break;
    case 10:
        Control(CC_PANPOT, seq_access_num, seq_num, value);
        break;
    case 11:
        Control(CC_EXPRESSION, seq_access_num, seq_num, value);
        break;
    case 64:
        Control(CC_DAMPER, seq_access_num, seq_num, value);
        break;
    case 91:
        Control(CC_EXTERNAL, seq_access_num, seq_num, value);
        break;
    case 98:
        Control(CC_NRPN1, seq_access_num, seq_num, value);
        break;
    case 99:
        Control(CC_NRPN2, seq_access_num, seq_num, value);
        break;
    case 100:
        Control(CC_RPN1, seq_access_num, seq_num, value);
        break;
    case 101:
        Control(CC_RPN2, seq_access_num, seq_num, value);
        break;
    case 121:
        ((void (*)(short, short))SsFCALL.control[CC_RESETALL])(
            seq_access_num, seq_num);
        break;
    default:
        score->delta_value = _SsReadDeltaValue(seq_access_num, seq_num);
        break;
    }
}
#endif
