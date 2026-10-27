#include <psyz.h>
#include <psyz/log.h>
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include "../internal.h"

#define N_CHANNELS 2
#define SAMPLE_SIZE sizeof(short)

static SDL_AudioStream* sdl_stream;
static unsigned output_volume = 100;
int Psyz_AudioSetOutputVolume(unsigned percent) {
    if (percent > 100)
        return -1;
    if (sdl_stream &&
        !SDL_SetAudioStreamGain(sdl_stream, (float)percent / 100.0f))
        return -1;
    output_volume = percent;
    return 0;
}
unsigned Psyz_AudioGetOutputVolume(void) { return output_volume; }

/* Game speed: the device pulls SPU samples that much faster, keeping every
 * voice, CD stream and sequence in step with the game (higher pitch). */
static unsigned audio_speed = 1;
void Sdl3Audio_SetSpeed(unsigned speed) {
    audio_speed = speed;
    if (sdl_stream)
        SDL_SetAudioStreamFrequencyRatio(sdl_stream, (float)speed);
}

static SDL_Mutex* mutex;
static bool video_clock, video_paused;
static unsigned video_remainder;
static bool video_pal;

static PsyzAudioCaptureCallback capture_callback;
static void* capture_userdata;

void Psyz_AudioSetCaptureCallback(
    PsyzAudioCaptureCallback callback, void* userdata) {
    Psyz_AudioLock();
    capture_callback = callback;
    capture_userdata = userdata;
    Psyz_AudioUnlock();
}

// Audio callback: SDL pulls audio from the SPU via this callback.
// The audio driver synchronizes the SPU based on the pulled samples.
static void SDLCALL audio_callback(void* userdata, SDL_AudioStream* stream,
                                   int additional_amount, int total_amount) {
    (void)userdata;
    (void)total_amount;
    if (additional_amount <= 0)
        return;

    int num_frames = additional_amount / (N_CHANNELS * SAMPLE_SIZE);
    short buf[4096] = {0};
    SDL_LockMutex(mutex);
    while (num_frames > 0) {
        int batch = num_frames;
        if (batch > 2048)
            batch = 2048;
        Psyz_SpuPullSamples(buf, batch);
        if (capture_callback)
            capture_callback(buf, batch, capture_userdata);
        SDL_PutAudioStreamData(stream, buf, batch * N_CHANNELS * SAMPLE_SIZE);
        num_frames -= batch;
    }
    SDL_UnlockMutex(mutex);
}

static bool is_audio_init = false;
int Psyz_AudioInit(void) {
    if (is_audio_init) {
        return 0;
    }
    Psyz_SpuInit();
    const char* clock = SDL_getenv("PSYZ_AUDIO_CLOCK");
    video_clock = clock && SDL_strcmp(clock, "video") == 0;
    if (video_clock) {
        mutex = SDL_CreateMutex();
        if (!mutex)
            return -1;
        video_paused = false;
        video_remainder = 0;
        video_pal = false;
        is_audio_init = true;
        return 0;
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            ERRORF("failed to init SDL audio: %s", SDL_GetError());
            return -1;
        }
    }

    mutex = SDL_CreateMutex();
    if (!mutex) {
        ERRORF("failed to create audio mutex: %s", SDL_GetError());
        return -1;
    }

    SDL_AudioSpec spec = {
        .format = SDL_AUDIO_S16,
        .channels = N_CHANNELS,
        .freq = PSYZ_SPU_SAMPLE_RATE,
    };
    sdl_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
    if (!sdl_stream) {
        ERRORF("failed to open audio device: %s", SDL_GetError());
        SDL_DestroyMutex(mutex);
        mutex = NULL;
        return -1;
    }

    SDL_SetAudioStreamGain(sdl_stream, (float)output_volume / 100.0f);
    SDL_SetAudioStreamFrequencyRatio(sdl_stream, (float)audio_speed);
    if (!SDL_ResumeAudioStreamDevice(sdl_stream)) {
        WARNF("failed to resume audio device: %s", SDL_GetError());
    }

    is_audio_init = true;
    DEBUGF("audio initialized");
    return 0;
}

void Psyz_AudioDestroy(void) {
    if (sdl_stream) {
        SDL_DestroyAudioStream(sdl_stream);
        sdl_stream = NULL;
    }
    if (mutex) {
        SDL_DestroyMutex(mutex);
        mutex = NULL;
    }
    capture_callback = NULL;
    capture_userdata = NULL;
    is_audio_init = false;
    video_clock = false;
}

void Psyz_AudioLock() {
    if (mutex) {
        SDL_LockMutex(mutex);
    }
}

void Psyz_AudioUnlock() {
    if (mutex) {
        SDL_UnlockMutex(mutex);
    }
}

void Psyz_AudioPause(void) {
    video_paused = true;
    if (sdl_stream) {
        SDL_PauseAudioStreamDevice(sdl_stream);
    }
}

void Psyz_AudioUnpause(void) {
    video_paused = false;
    if (sdl_stream) {
        SDL_ResumeAudioStreamDevice(sdl_stream);
    }
}

int Psyz_AudioIsPaused(void) {
    if (video_clock)
        return !is_audio_init || video_paused;
    return !sdl_stream || SDL_AudioStreamDevicePaused(sdl_stream);
}

static void MixOfflineFrames(unsigned frames) {
    short samples[2 * 883];
    unsigned offset = 0;
    while (offset < frames) {
        unsigned batch = Psyz_KernelAudioBatch(frames - offset);
        Psyz_SpuPullSamples(samples + offset * 2, (int)batch);
        Psyz_KernelAdvanceAudio(batch);
        offset += batch;
    }
    if (capture_callback)
        capture_callback(samples, (int)frames, capture_userdata);
}

int Psyz_AudioWaitForTimer(void) {
    if (!is_audio_init || !video_clock || video_paused)
        return 0;
    SDL_LockMutex(mutex);
    MixOfflineFrames(Psyz_KernelAudioBatch(883));
    SDL_UnlockMutex(mutex);
    return 1;
}

/* SDL backend clock for offline runs. Queries and wall time never consume
 * samples. Carry the NTSC fraction so 60000 blanks yield 44100*1001 frames. */
void Sdl3Audio_VBlank(bool pal) {
    if (!is_audio_init || !video_clock || video_paused)
        return;
    if (pal != video_pal)
        video_remainder = 0;
    video_pal = pal;
    unsigned divisor = pal ? 50 : 60000;
    video_remainder += PSYZ_SPU_SAMPLE_RATE * (pal ? 1u : 1001u);
    unsigned frames = video_remainder / divisor;
    video_remainder %= divisor;
    SDL_LockMutex(mutex);
    MixOfflineFrames(frames);
    SDL_UnlockMutex(mutex);
}
