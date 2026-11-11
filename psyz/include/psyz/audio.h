#ifndef PSYZ_AUDIO_H
#define PSYZ_AUDIO_H

/**
 * @file audio.h
 * @brief Host audio subsystem backend endpoints.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Observe playback PCM after mixing, without pulling additional samples.
 * Frames are interleaved signed 16-bit stereo at 44100 Hz, in host byte order.
 * The buffer is valid only during the callback and must not be modified.
 * Runs under the audio lock, on the SDL/PSP audio thread or video-clock thread.
 * Do not call audio/SPU APIs from the callback. Keep device callbacks brief.
 * Pass NULL to detach; return guarantees the old callback is no longer running.
 * Registration may precede init; AudioDestroy clears it. Direct SpuPullSamples
 * calls bypass this observer. Paused playback emits no callbacks.
 */
typedef void (*PsyzAudioCaptureCallback)(const short* samples, int frames,
                                         void* userdata);
void Psyz_AudioSetCaptureCallback(PsyzAudioCaptureCallback callback,
                                  void* userdata);

/**
 * @brief Initialize the audio subsystem
 *
 * Opens the host audio device and the SPU mixer. Idempotent.
 * With PSYZ_AUDIO_CLOCK=video, opens no device and advances the mixer once
 * per blocking SDL video VSync instead (one PS1 field: NTSC 59.817 Hz or PAL
 * 49.747 Hz).
 * Pause also suspends this offline clock; direct sample pulls remain available.
 *
 * @return 0 on success, -1 on failure
 */
int Psyz_AudioInit(void);

/**
 * @brief Shut down the audio subsystem and release its resources
 *
 * Typically used to undo the audio subsystem init for audio offline rendering.
 */
void Psyz_AudioDestroy(void);

/**
 * Advance offline playback toward the next emulated timer edge while a native
 * synchronous I/O routine waits for its timer handler (at most 887 frames per
 * call). Does not advance VBlank.
 * Returns 1 when the video-clock backend generated samples, otherwise 0:
 * real-time backends already run independently and the caller should poll/wait
 * as usual. Paused/uninitialized audio returns 0. Do not call under audio lock.
 */
int Psyz_AudioWaitForTimer(void);

/**
 * @brief Pause the host audio device so it stops pulling samples from the SPU
 *
 * Psyz_AudioUnpause must be called to resume.
 */
void Psyz_AudioPause(void);
int Psyz_AudioIsPaused(void);
/** Host output gain only; does not alter emulated SPU registers or capture PCM.
 * SDL backend, range 0..100, can be selected before audio initialization. */
int Psyz_AudioSetOutputVolume(unsigned percent);
unsigned Psyz_AudioGetOutputVolume(void);

/**
 * @brief Resume audio playback after Psyz_AudioPause
 */
void Psyz_AudioUnpause(void);

/**
 * @brief Acquire the audio mutex
 *
 * Intended for tests and offline rendering that need to suspend the SDL audio
 * callback while pulling samples directly. Must be paired with
 * Psyz_AudioUnlock.
 */
void Psyz_AudioLock(void);

/**
 * @brief Release the audio mutex acquired with Psyz_AudioLock
 */
void Psyz_AudioUnlock(void);

#ifdef __cplusplus
}
#endif

#endif
