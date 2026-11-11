// Private header for PSY-Z internal macros
// Not part of the public API - do not include from external code

#ifndef PSYZ_INTERNAL_H
#define PSYZ_INTERNAL_H

#define INCLUDE_ASM(path, func)
#define WEAK_INCLUDE_ASM(path, func)
#define NOP

#ifndef LEN
#define LEN(x) ((s32)(sizeof(x) / sizeof(*(x))))
#endif

#ifndef LENU
#define LENU(x) ((u32)(sizeof(x) / sizeof(*(x))))
#endif

#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

#define VRAM_W 1024
#define VRAM_H 512

// SDL platform lifecycle code uses this to avoid undoing an explicit game
// audio pause when the application returns from the background.
int Psyz_AudioIsPaused(void);

// Offline sample clock and root-counter deadlines; no host-time advancement.
unsigned Psyz_KernelAudioBatch(unsigned maximum);
void Psyz_KernelAdvanceAudio(unsigned frames);
void Psyz_KernelPauseHostClock(int paused);
void Psyz_KernelSetHostClockSpeed(unsigned speed);

// Discard prefetched external CD input without resetting SPU voices/reverb.
void Psyz_SpuFlushCdSamples(void);

// Delivers a kernel event as an emulated device interrupt would: right away,
// or at ExitCriticalSection when the caller is inside a critical section.
void Psyz_KernelRaise(unsigned int desc, unsigned int spec);

// Runs fn as an interrupt on the kernel's interrupt thread. Never blocks, so
// the audio thread can use it.
void Psyz_KernelPost(void (*fn)(void));

// Completes a native synchronous DMA transfer without recursive callbacks.
void Psyz_DmaComplete(unsigned channel);

// Raises the vblank interrupt; VSync calls it once per emulated frame.
void Psyz_KernelVBlank(void);

// Delivers the sectors a callback-driven CD read has reached (libcd.c).
void Psyz_CdServiceDrive(void);

// Services due timers when the host has no thread to run them.
void Psyz_KernelPoll(void);

// Subpixel GTE->GPU coordinate cache for optional geometry correction.
/* Bounded diagnostic only; no cache entries, addresses or rendering policy altered. */
typedef struct {
    unsigned long long matrix_high, matrix_integer, xy_clip, xy_disagree;
    unsigned long long lookup_hit, lookup_miss, copy_hit, copy_miss;
    unsigned samples;
    struct { float expected_x, expected_y, actual_x, actual_y, depth; unsigned reason; } sample[2];
} PsyzPgxpReasons;
void Psyz_PgxpTraceReasonsEnable(int enabled);
void Psyz_PgxpTraceReasonsTake(PsyzPgxpReasons *out);
void Psyz_PgxpSetActive(int active);
void Psyz_PgxpNextFrame(void);
void Psyz_PgxpSetCulling(int enabled);
void Psyz_PgxpSetEffectEnabled(int enabled);
void Psyz_GteCopyQueuedPgxpSxy(
    const void* src, const void* dst, unsigned int sxy);
int Psyz_PgxpLookupVertex(const void* packet_sxy_addr, unsigned int sxy,
                          float* x, float* y, float* w);

#endif
