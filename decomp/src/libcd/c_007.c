#include <common.h>
#include <libcd.h>
#include "libcd_stream.h"

u_long StFreeRing(u_long* base) {
    s32 temp_a1;
    s32 i;
    s16 nSectors;
    StHEADER* temp_v0;

    temp_a1 = ((u32*)base - (u32*)&StRingAddr[StRingSize]) / 504;
    temp_v0 = &StRingAddr[temp_a1];
    nSectors = StRingAddr[temp_a1].nSectors;
    if ((s16)temp_v0->id != 4) {
        return 1;
    }
    for (i = 0; i < nSectors; i++) {
        *((s16*)((u_char*)StRingAddr + ((i + temp_a1) << 5))) = 0;
    }
    /* An exact-capacity frame ends at the payload boundary, not another header. */
    StRingIdx3 = i + temp_a1;
    if (StRingIdx3 == StRingSize) {
        StRingIdx3 = 0;
    }
    return 0;
}
