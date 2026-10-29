#include <psyz.h>
#include <libspu.h>
#include <psyz/log.h>
#include "../../decomp/src/libspu/libspu_private.h"

static SPU_RXX spu_RXX;
static unsigned _dma_dpcr;
union SpuUnion* _spu_RXX = (union SpuUnion*)&spu_RXX;
unsigned* dma_dpcr = &_dma_dpcr; // TODO this must be removed
volatile u16 _spu_RQ[10] = {0};

void SpuNGetVoiceAttr(int vNum, SpuVoiceAttr* arg) { NOT_IMPLEMENTED; }

void SpuGetVoiceEnvelope(int voiceNum, short* envx) {
    if (!envx)
        return;
    *envx = voiceNum >= 0 && voiceNum < NUM_VOICES
                ? (short)SPURV(voiceNum, volumex)
                : 0;
}

void SpuSetReverbModeDepth(short depthL, short depthR) {
    SPUW(rev_vol.left, (u16)depthL);
    SPUW(rev_vol.right, (u16)depthR);
}

int SpuSetReverbModeType(unsigned int mode) {
    SpuReverbAttr attr = {0};
    attr.mask = SPU_REV_MODE;
    attr.mode = (int)mode;
    return (int)SpuSetReverbModeParam(&attr);
}
