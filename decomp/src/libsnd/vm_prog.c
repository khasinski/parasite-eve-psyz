#include "libsnd_private.h"

#ifndef __psyz
INCLUDE_ASM("asm/nonmatchings/libsnd/vm_prog", _SsVmSetProgVol);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_prog", _SsVmGetProgVol);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_prog", _SsVmSetProgPan);

INCLUDE_ASM("asm/nonmatchings/libsnd/vm_prog", _SsVmGetProgPan);
#else
int _SsVmSetProgVol(short vabId, short prog, unsigned char vol) {
    if (_SsVmVSetUp(vabId, prog) != 0) {
        return -1;
    }
    _svm_pg[prog].mvol = vol;
    return _svm_pg[prog].mvol;
}

int _SsVmGetProgVol(short vabId, short prog) {
    if (_SsVmVSetUp(vabId, prog) != 0) {
        return -1;
    }
    return _svm_pg[prog].mvol;
}

int _SsVmSetProgPan(short vabId, short prog, unsigned char pan) {
    if (_SsVmVSetUp(vabId, prog) != 0) {
        return -1;
    }
    _svm_pg[prog].mpan = pan;
    return _svm_pg[prog].mpan;
}

int _SsVmGetProgPan(short vabId, short prog) {
    if (_SsVmVSetUp(vabId, prog) != 0) {
        return -1;
    }
    return _svm_pg[prog].mpan;
}
#endif
