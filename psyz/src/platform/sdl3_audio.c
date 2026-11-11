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
/* Offline clock: samples left in the current field, until the next blank. A
 * native busy wait (Psyz_AudioWaitUs) spends part of a field, or crosses
 * blanks; VSync(0) then mixes only the rest of the field. */
static unsigned field_frames_left;
static unsigned wait_fraction; // carried sub-sample part of waited time

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
        field_frames_left = 0;
        wait_fraction = 0;
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

/* SDL backend clock for offline runs. Queries and wall time never consume
 * samples. A field lasts 3413 x 263 (NTSC) or 3406 x 314 (PAL) GPU clocks,
 * 451584/715909 or 451584/709379 system clocks each, and a 44100 Hz sample
 * 768 system clocks: 897619 * 588 / 715909 (about 737.24) or
 * 1069484 * 588 / 709379 (about 886.49) samples, the fraction carried. */
#define FIELD_SAMPLES_NTSC (897619u * 588u)
#define FIELD_DIVISOR_NTSC 715909u
#define FIELD_SAMPLES_PAL (1069484u * 588u)
#define FIELD_DIVISOR_PAL 709379u
#define FIELD_FRAMES_MAX 887 // a PAL field, rounded up

/* Device interrupts fire while samples are mixed (Psyz_KernelAdvanceAudio);
 * a wait inside one spends no further time. */
static int mixing;

static void MixOfflineFrames(unsigned frames) {
    short samples[2 * FIELD_FRAMES_MAX];
    unsigned offset = 0;
    mixing++;
    while (offset < frames) {
        unsigned batch = Psyz_KernelAudioBatch(frames - offset);
        Psyz_SpuPullSamples(samples + offset * 2, (int)batch);
        Psyz_KernelAdvanceAudio(batch);
        offset += batch;
    }
    mixing--;
    if (capture_callback)
        capture_callback(samples, (int)frames, capture_userdata);
}

int Psyz_AudioWaitForTimer(void) {
    if (!is_audio_init || !video_clock || video_paused)
        return 0;
    SDL_LockMutex(mutex);
    MixOfflineFrames(Psyz_KernelAudioBatch(FIELD_FRAMES_MAX));
    SDL_UnlockMutex(mutex);
    return 1;
}

/* Length of the next field in samples, the fraction carried. */
static unsigned NextFieldFrames(bool pal) {
    if (pal != video_pal)
        video_remainder = 0;
    video_pal = pal;
    unsigned divisor = pal ? FIELD_DIVISOR_PAL : FIELD_DIVISOR_NTSC;
    video_remainder += pal ? FIELD_SAMPLES_PAL : FIELD_SAMPLES_NTSC;
    unsigned frames = video_remainder / divisor;
    video_remainder %= divisor;
    return frames;
}

/* A native busy wait spends emulated time as a PS1 spinning on a device
 * would: blanks it crosses happen during the wait (Psyz_VideoWaitBlank:
 * counter, VSync interrupt), so the field count stays on the same clock as
 * the root counters, the CD drive and the mixed audio. */
/* The offline clock runs from the first blank, not only once the game
 * initialises sound: root counters and the CD drive use it from boot. */
static void StartOfflineClock(void) {
    if (!is_audio_init && Psyz_KernelOfflineClock())
        Psyz_AudioInit();
}

int Psyz_AudioWaitUs(uint64_t us) {
    StartOfflineClock();
    if (!is_audio_init || !video_clock || video_paused)
        return 0;
    if (mixing)
        return 1;
    uint64_t scaled = us * PSYZ_SPU_SAMPLE_RATE + wait_fraction;
    uint64_t frames = scaled / 1000000;
    wait_fraction = (unsigned)(scaled % 1000000);
    while (frames) {
        if (!field_frames_left)
            field_frames_left = NextFieldFrames(video_pal);
        unsigned batch = field_frames_left;
        if (batch > FIELD_FRAMES_MAX)
            batch = FIELD_FRAMES_MAX;
        if (batch > frames)
            batch = (unsigned)frames;
        SDL_LockMutex(mutex);
        MixOfflineFrames(batch);
        SDL_UnlockMutex(mutex);
        frames -= batch;
        field_frames_left -= batch;
        if (!field_frames_left) {
            field_frames_left = NextFieldFrames(video_pal);
            Psyz_VideoWaitBlank();
        }
    }
    return 1;
}

/* A blank VSync(0) waited for: the rest of the current field. */
void Sdl3Audio_VBlank(bool pal) {
    StartOfflineClock();
    if (!is_audio_init || !video_clock || video_paused)
        return;
    if (!field_frames_left || pal != video_pal)
        field_frames_left = NextFieldFrames(pal);
    SDL_LockMutex(mutex);
    MixOfflineFrames(field_frames_left);
    SDL_UnlockMutex(mutex);
    field_frames_left = 0;
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "psyz/state_audio.h"
static SDL_ThreadID state_audio_owner;
int Psyz_StateAudioOwned(void){return state_audio_owner&&state_audio_owner==SDL_GetCurrentThreadID();}
int Psyz_StateAudioBegin(void){if(!is_audio_init||!mutex||Psyz_StateAudioOwned())return 0;SDL_LockMutex(mutex);if(state_audio_owner){SDL_UnlockMutex(mutex);return 0;}state_audio_owner=SDL_GetCurrentThreadID();return 1;}
void Psyz_StateAudioEnd(void){if(Psyz_StateAudioOwned()){state_audio_owner=0;SDL_UnlockMutex(mutex);}}
int Psyz_StateAudioDiscardQueued(void){return Psyz_StateAudioOwned()&&(!sdl_stream||SDL_ClearAudioStream(sdl_stream));}
#endif

#if PE_NATIVE_STATE_CPU_COMPONENTS
static void state_audio_clock_put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t state_audio_clock_get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int Psyz_StateAudioClockCapture(unsigned char *out,size_t n){if(!out||n!=20||!Psyz_StateAudioOwned())return 0;memcpy(out,"PEAUDCLK",8);state_audio_clock_put(out+8,video_clock);state_audio_clock_put(out+12,video_pal);state_audio_clock_put(out+16,video_remainder);return 1;}
int Psyz_StateAudioClockValidate(const unsigned char *in,size_t n){return in&&n==20&&Psyz_StateAudioOwned()&&!memcmp(in,"PEAUDCLK",8)&&state_audio_clock_get(in+8)==(uint32_t)video_clock&&state_audio_clock_get(in+12)<=1&&state_audio_clock_get(in+16)<(state_audio_clock_get(in+12)?FIELD_DIVISOR_PAL:FIELD_DIVISOR_NTSC);}
int Psyz_StateAudioClockRestore(const unsigned char *in,size_t n){if(!Psyz_StateAudioClockValidate(in,n))return 0;video_pal=state_audio_clock_get(in+12)!=0;video_remainder=state_audio_clock_get(in+16);field_frames_left=0;wait_fraction=0;return 1;}
#endif
