#include <common.h>
#include <libsnd.h>
#include <libspu.h>
#include <libetc.h>

void _SsInit(void);
void SsInit(void) {
    ResetCallback();
    SpuInit();
#ifdef __psyz
    /* PSY-Q 4.x clears the reverb work area of the largest mode. */
    SpuClearReverbWorkArea(SPU_REV_MODE_ECHO);
#endif
    _SsInit();
}
