#ifndef PSYZ_SYSTEM_H
#define PSYZ_SYSTEM_H

/**
 * @file system.h
 * @brief Host filesystem shims and PlayStation 1 path adjustment helpers.
 */

#ifdef __psyz // exclude when targeting the PSX
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* BIOS random generator, separate from the host C library. The state getter
 * plus Psyz_Srand allow deterministic save/restore without consuming a value. */
int Psyz_Rand(void);
void Psyz_Srand(unsigned int seed);
unsigned int Psyz_RandState(void);

// A native PS1 build uses PSY-Q's own file I/O.
#ifndef __psx__

#define open psyz_open
#define close psyz_close
#define lseek psyz_lseek
#define read psyz_read
#define write psyz_write
#define ioctl psyz_ioctl
#define undelete psyz_undelete
#define _get_errno psyz_get_errno

#ifdef _WIN32
int psyz_open(const char* devname, int flag, ...);
int psyz_close(int fd);
unsigned long psyz_lseek(int fd, unsigned int offset, int flag);
int psyz_read(int fd, void* buf, unsigned int n);
int psyz_write(int fd, const void* buf, unsigned int n);
long psyz_ioctl(int fd, int com, int arg);
#else
int psyz_open(const char* devname, int flag);
int psyz_close(int fd);
unsigned long psyz_lseek(int fd, unsigned int offset, int flag);
long psyz_read(long fd, void* buf, long n);
int psyz_write(int fd, char* buf, int n);
long psyz_ioctl(int fd, int com, int arg);
#endif

#endif // __psx__

#ifdef _MSC_VER
#define __builtin_memcpy memcpy
#endif

/**
 * @brief Adjust a PlayStation 1 path to the host filesystem
 *
 * Handles memory card paths (bu00:, bu10:, etc.) and other special cases.
 * If a custom callback is set via Psyz_AdjustPathCB and returns >= 0, the
 * callback result is used; otherwise internal adjustment is applied. The
 * function is used internally when opening and creating files, or when
 * enumerating the list of files in the specified directory.
 *
 * @note The filename portion (after the last path separator) is automatically
 *       truncated to 20 characters to match PS1 DIRENTRY.name[20].
 *       A full directory name uses all 20 bytes without a trailing NUL.
 *       This truncation is applied regardless of whether callback or internal
 *       adjustment is used.
 *
 * @param dst Destination buffer for adjusted path (must be valid)
 * @param src Source path to adjust (PlayStation 1 format)
 * @param maxlen Maximum length of destination buffer
 */
void Psyz_AdjustPath(char* dst, const char* src, int maxlen);

/**
 * @brief Set custom path adjustment callback
 *
 * The callback is invoked before internal path adjustment. Its return value:
 * - < 0: No adjustment done, fall back to PSY-Z internal adjustment
 * - >= 0: Number of bytes written to dst (adjustment successful)
 *
 * The callback receives:
 * - dst: Destination buffer for adjusted path
 * - src: Source path to adjust
 * - maxlen: Maximum length of destination buffer
 *
 * @param callback Path adjustment callback
 */
void Psyz_AdjustPathCB(int (*callback)(char* dst, const char* src, int maxlen));

/**
 * @brief Return non-zero when the host requested application termination.
 *
 * SDL targets set this after SDL_EVENT_QUIT. Console/mobile targets may set it
 * from their platform lifecycle callback. Games that want graceful shutdown can
 * poll this from their main loop instead of relying on a host-side exit().
 */
int Psyz_QuitRequested(void);

/**
 * @brief Join two path components with the platform's path separator
 *
 * Automatically handles path separators to avoid double separators.
 *
 * @param left Left path component (modified in-place)
 * @param right Right path component to append
 * @param maxlen Maximum length of left buffer
 * @return Pointer to left on success, NULL if buffer too small
 */
char* Psyz_JoinPath(char* left, const char* right, int maxlen);

/**
 * @brief Callback fired once per emulated vsync.
 */
typedef void (*PsyzVSyncCb)(void);

/**
 * @brief Register a callback fired once per libapi VSync.
 *
 * Used to set custom VSync callbacks that do not replace the existing
 * VSyncCallback set by the game. The registered callback will be executed
 * right before the callback set with VSyncCallback.
 *
 * Only one callback can be registered at a time. If a callback is already
 * registered, the caller is responsible for chaining to the previously
 * registered callback from within its own.
 *
 * @param cb Callback to register, or NULL to unregister
 * @return Previously registered callback, or NULL if none was set
 */
PsyzVSyncCb Psyz_SetVSyncCb(PsyzVSyncCb cb);

/**
 * @brief Spend emulated time as the PS1 CPU would on native work
 *
 * Native code runs in no emulated time. Where a game's timing depends on
 * how long the PS1 took for a computation or a busy loop (a frame decoded
 * in software, a poll loop with a retry count), it can account for that
 * time here: on the offline clock (PSYZ_AUDIO_CLOCK=video) the time is
 * mixed, with the blanks it crosses happening meanwhile (VSync callbacks,
 * the VSync count); on a real-time clock the host waits until that much
 * time has passed since the previous call's deadline, so a run of short
 * calls adds up exactly.
 *
 * @param ns Nanoseconds of PS1 time
 */
void Psyz_SpendTime(unsigned ns);

/**
 * @brief Complete MDEC output transfers at once
 *
 * By default the MDEC takes its PS1 time per macroblock and an output
 * transfer raises its DMA callback once its data is decoded, on the
 * emulated clock.
 *
 * @param instant Non-zero for instant transfers
 */
void Psyz_MdecSetInstantTiming(int instant);

/**
 * @brief Account for a memory card ("bu") file transfer of `bytes`
 *
 * For a host-side BIOS file layer. An asynchronous (FASYNC) transfer
 * raises SwCARD/EvSpIOE once the PS1 would have finished it, on the
 * emulated clock; a synchronous one spends that time before returning.
 */
void Psyz_CardFileTransfer(long bytes, int async);

/**
 * @brief Spend the time the BIOS takes to create a card file
 */
void Psyz_CardFileCreate(void);

/**
 * @brief Complete memory card requests and file I/O at once
 *
 * By default the BIOS card driver serves the two ports on alternate
 * vblanks, a sector transfer per turn, and an empty port times out a vblank
 * later (_card_info, _card_load, _card_read/_card_write, _bu_init).
 * PSYZ_CARD_TIMING=instant does the same.
 *
 * @param instant Non-zero for instant transfers
 */
void Psyz_CardSetInstantTiming(int instant);

/**
 * @brief Start a raw command on memory card channel `chan`
 *
 * For a host-side library that talks to the card port itself (libmcx):
 * the card driver transfers it on that port's next vblank and it is
 * answered at the vblank after. Returns 0 while a command is in progress.
 */
int Psyz_CardRawBegin(long chan);

/**
 * @brief Poll (wait = 0) or wait for the raw command
 *
 * @return 1 once answered (the command is then finished), 0 while it runs,
 *         -1 when none was started
 */
int Psyz_CardRawSync(int wait);

/** Memory card slot states (Psyz_CardSlotState). */
enum { PSYZ_CARD_NONE, PSYZ_CARD_FORMATTED, PSYZ_CARD_UNFORMATTED };

/**
 * @brief What memory card port `port` (0 or 1) holds
 *
 * A formatted card by default; PSYZ_CARD_UNPLUG / PSYZ_CARD_UNFORMATTED
 * list slots (1, 2) that are empty or hold an unformatted card.
 */
int Psyz_CardSlotState(int port);

/**
 * @brief Plug, unplug or unformat the card in port `port` (0 or 1)
 */
void Psyz_CardSetSlotState(int port, int state);

#ifdef __cplusplus
}
#endif

#endif
