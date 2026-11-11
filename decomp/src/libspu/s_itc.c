#include "libspu_private.h"
#include "libapi.h"

long SpuIsTransferCompleted(long immediateFlag) {
#ifdef __psyz
    /* The DMA end is the emulated transfer's (spu.c), not the event. */
    int Psyz_SpuDmaBusy(void);
    void Psyz_SpuDmaWait(void);
    void Psyz_SpendTime(unsigned ns);
    if (_spu_trans_mode == 1 || _spu_inTransfer == 1) {
        return 1;
    }
    if (immediateFlag == 1) {
        Psyz_SpuDmaWait();
    } else if (Psyz_SpuDmaBusy()) {
        Psyz_SpendTime(1000); // the PS1 time of a poll
        return 0;
    }
    _spu_inTransfer = 1;
    return 1;
#endif
    s32 in_transfer;

    if (_spu_trans_mode == 1 || _spu_inTransfer == 1) {
        return 1;
    }
    in_transfer = TestEvent(_spu_EVdma);
    if (immediateFlag == 1) {
        if (in_transfer == 0) {
#ifndef __psyz // HACK the TestEvent must work
            do {
            } while (TestEvent(_spu_EVdma) == 0);
#endif
        }
        in_transfer = 1;
        _spu_inTransfer = in_transfer;
        return in_transfer;
    }
    if (in_transfer == 1) {
        _spu_inTransfer = in_transfer;
    }
    return in_transfer;
}
