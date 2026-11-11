#include <psyz.h>
#include <libspu.h>
#include <libsnd.h>
#include <psyz/log.h>
#include "../../decomp/src/libspu/libspu_private.h"
#include "../../decomp/src/libsnd/libsnd_private.h"

#define LEN(x) ((s32)(sizeof(x) / sizeof(*(x))))
#define NUM_VOICES 24

typedef void (*SndSsMarkCallbackProc)(short seq_no, short sep_no, short data);

extern short _snd_seq_s_max;
extern short _snd_seq_t_max;
extern int _snd_ev_flag;
extern _SsFCALL SsFCALL;
extern SndSsMarkCallbackProc _SsMarkCallback[32][16];
extern unsigned int VBLANK_MINUS;
extern int _snd_openflag;

static void SetVoiceData(int nVoice, unsigned short* data) {
    for (int i = 0; i < 8; i++) {
        Psyz_SpuWrite(nVoice * 0x10 + i * 2, data[i]);
    }
}

static void SetStateData(unsigned short* data, unsigned nWords) {
    for (unsigned i = 0; i < nWords; i++) {
        Psyz_SpuWrite(0x180 + i * 2, data[i]);
    }
}

static unsigned short default_voice[] = {0, 0, 0x1000, 0x3000, 0x00BF, 0, 0, 0};
static unsigned short default_state[] = {
    0x3FFF, 0x3FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
extern SPU_RXX* _svm_sreg;

void _SsInit(void) {
    int i, j;

    _svm_sreg = (SPU_RXX*)_spu_RXX;
    for (i = 0; i < NUM_VOICES; i++) {
        SetVoiceData(i, default_voice);
    }
    SetStateData(default_state, LEN(default_state));

    _SsVmInit(NUM_VOICES);
    for (j = 0; j < LEN(_SsMarkCallback); j++) {
        for (i = 0; i < LEN(*_SsMarkCallback); i++) {
            _SsMarkCallback[j][i] = NULL;
        }
    }

    VBLANK_MINUS = 60;
    _snd_openflag = 0;
    _snd_ev_flag = 0;
}

void SsSeqSetDecrescendo(short seq_access_num, short vol, long v_time) {
    NOT_IMPLEMENTED;
}

void SetAutoVol(short voice) { NOT_IMPLEMENTED; }

void SeAutoPan(short arg0, short arg1, short arg2, short arg3) {
    NOT_IMPLEMENTED;
}
