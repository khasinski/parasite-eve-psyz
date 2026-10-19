#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/ut_sva", SsUtSetVagAtr);

#else
short SsUtSetVagAtr(short vabId, short prog, short toneNum, VagAtr* vagatrptr) {
    VagAtr* tn;

    if (vabId < 0 || vabId >= NUM_VAB || _svm_vab_used[vabId] != 1) {
        return -1;
    }
    if (_SsVmVSetUp(vabId, prog) != 0) {
        return -1;
    }
    tn = &_svm_tn[toneNum + _svm_cur.fake_program * 16];
    tn->prior = vagatrptr->prior;
    tn->mode = vagatrptr->mode;
    tn->vol = vagatrptr->vol;
    tn->pan = vagatrptr->pan;
    tn->center = vagatrptr->center;
    tn->shift = vagatrptr->shift;
    tn->min = vagatrptr->min;
    tn->max = vagatrptr->max;
    tn->vibW = vagatrptr->vibW;
    tn->vibT = vagatrptr->vibT;
    tn->porW = vagatrptr->porW;
    tn->porT = vagatrptr->porT;
    tn->pbmin = vagatrptr->pbmin;
    tn->pbmax = vagatrptr->pbmax;
    tn->adsr1 = vagatrptr->adsr1;
    tn->adsr2 = vagatrptr->adsr2;
    tn->prog = vagatrptr->prog;
    tn->vag = vagatrptr->vag;
    return 0;
}
#endif
