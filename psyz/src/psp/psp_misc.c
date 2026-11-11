// PSP stubs to no-op PC specific functions
#include <psyz.h>
#include <psyz/log.h>
#include <libgpu.h>
#include "../draw.h"

void Psyz_SetTitle(const char* str) { (void)str; }

void Psyz_SetWindowScale(int scale) { (void)scale; }

int Psyz_VideoSetInternalResolution(unsigned multiplier) {
    return multiplier == 1 ? 0 : -1;
}

unsigned Psyz_VideoGetInternalResolution(void) { return 1; }

int Psyz_VideoSetGeometryCorrection(int enabled) {
    (void)enabled;
    return -1;
}

int Psyz_VideoGetGeometryCorrection(void) { return 0; }

int Psyz_VideoSetDynamicLighting(int enabled) {
    (void)enabled;
    return -1;
}

int Psyz_VideoGetDynamicLighting(void) { return 0; }

int Psyz_VideoSetEffectGeometryCorrection(int enabled) {
    (void)enabled;
    return -1;
}

int Psyz_VideoGetEffectGeometryCorrection(void) { return 0; }

int Draw_SetHorizontalGrid(
    unsigned int source_width, unsigned int target_width) {
    if (source_width == 0 || target_width == 0) {
        return -1;
    }
    return source_width == target_width ? 0 : -1;
}

int Psyz_DebugServer(int port) {
    (void)port;
    WARNF("debug server not supported on this platform");
    return -1;
}

void Psyz_DebugServerStop(void) {}

int Psyz_VideoSetNativeRasterizer(int enabled) { return enabled ? -1 : 0; }
int Psyz_VideoGetNativeRasterizer(void) { return 0; }
int Psyz_VideoNativeRasterizerActive(void) { return 0; }
