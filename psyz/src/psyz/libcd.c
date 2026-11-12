#include <psyz.h>
#include <libetc.h>
#include <libcd.h>
#include <psyz/log.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <kernel.h>
#include "../internal.h"
#include "os.h"
#include "../../../decomp/src/libcd/libcd_stream.h"
#include "../../decomp/src/libspu/libspu_private.h"

#define SECTOR_SIZE 2352
#define MAX_TRACKS 99
#define MAX_PATH_LEN 512

// Track types
typedef enum {
    TRACK_AUDIO,
    TRACK_MODE1_2352,
    TRACK_MODE2_2352,
    TRACK_UNKNOWN
} TrackType;

// Track entry from CUE file
typedef struct {
    char file_path[MAX_PATH_LEN];
    int start_sector; // MSF sector offset within the file
    int abs_sector;   // Absolute sector position on the "virtual CD"
    int track_num;
    TrackType type;
    int is_valid;
} TrackEntry;

// https://www.problemkaputt.de/psx-spx.htm#cdromcontrollerioports
typedef struct {
    u_char cd_src_l_dst_l;  // volume for CD(L) -> SPU (L)
    u_char cd_src_l_dst_r;  // volume for CD(L) -> SPU (R)
    u_char cd_src_r_dst_l;  // volume for CD(R) -> SPU (L)
    u_char cd_src_r_dst_r;  // volume for CD(R) -> SPU (R)
    u_char adpcm_mute;      // 0=Normal, 1=Mute
    u_char is_stereo;       // 0=Mono, 1=Stereo
    u_char sample_rate;     // 0=37800Hz, 1=18900Hz
    u_char bits_per_sample; // 0=4bit, 1=8bit
    u_char emphasis;        // 0=Off, 1=Emphasis
} CdContext;

// Global CUE data storage
static TrackEntry g_tracks[MAX_TRACKS];
static int g_track_count = 0;
static char g_cue_base_path[MAX_PATH_LEN];

char* CD_comstr[] = {
    "CdlSync",    "CdlNop",
    "CdlSetloc",  "CdlPlay",
    "CdlForward", "CdlBackward",
    "CdlReadN",   "CdlStandby",
    "CdlStop",    "CdlPause",
    "CdlReset",   "CdlMute",
    "CdlDemute",  "CdlSetfilter",
    "CdlSetmode", "CdlGetparam",
    "CdlGetlocL", "CdlGetlocP",
    "?",          "CdlGetTN",
    "CdlGetTD",   "CdlSeekL",
    "CdlSeekP",   "?",
    "?",          "?",
    "?",          "CdlReadS",
    "?",          "?",
    "?",          "?",
};
char* CD_intstr[] = {
    "NoIntr",  "DataReady", "Complete", "Acknowledge",
    "DataEnd", "DiskError", "?",        "?",
};

CdlCB CD_cbsync = NULL;
CdlCB CD_cbready = NULL;
int CD_cbread = 0;
int CD_debug = 0;
int CD_status = 0;
int CD_status1 = 0;
int CD_nopen = 0;
CdlLOC CD_pos = {2, 0, 0, 0};
u_char CD_mode = 0;
u_char CD_com = 0;
int DS_active = 0;
static CdContext ctx;

int CD_cw(u8 com, u8* param, u_char* result, s32 arg3);
int CD_sync(int mode, u_char* result);

static void ADPMUTE(int mute) {
    // 0x1F801800 = 3
    ctx.adpcm_mute = mute != 0; // 0x1F801803 = mute & 1
}

static int enable_passthrough = 1;
static void CHNGATV() {
    // 0x1F801800 = 3
    // 0x1F801803 = 0x20

    // enabling passthrough avoid the transformation of the CD pulled samples
    // to be re-mixed with different volume values, leading an increase of
    // performance on weak/embedded targets.
    //
    // PS1 defaults CD_vol as 127,0,127,0 - if detected, enable passthrough.
    //
    // Volume mix algo on PsyZ is (sample * volume) / 128, meaning 128,0,128,0
    // would also return 1:1 samples - enable passthrough here too.
    //
    // volume at 127 and 128 would theoretically generate sound at different
    // levels of volume, but no normal human would ever notice such difference.
    enable_passthrough =
        (ctx.cd_src_l_dst_l == 127 || ctx.cd_src_l_dst_l == 128) &&
        (ctx.cd_src_r_dst_r == 127 || ctx.cd_src_r_dst_r == 128) &&
        ctx.cd_src_l_dst_r == 0 && ctx.cd_src_r_dst_l == 0;
}

static short clamp16(int v) {
    if (v < -32768)
        return -32768;
    if (v > 32767)
        return 32767;
    return (short)v;
}

// Trim whitespace from both ends of a string
static char* str_trim(char* str) {
    char* end;
    while (isspace((unsigned char)*str)) {
        str++;
    }
    if (*str == 0) {
        return str;
    }
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    return str;
}

// Extract quoted string or unquoted token
static char* extract_token(char* str, char* dest, int max_len) {
    char* p = str;
    int len = 0;

    // Skip leading whitespace
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '"') {
        // Quoted string
        p++;
        while (*p && *p != '"' && len < max_len - 1) {
            dest[len++] = *p++;
        }
        if (*p == '"') {
            p++;
        }
    } else {
        // Unquoted token (up to space)
        while (*p && !isspace((unsigned char)*p) && len < max_len - 1) {
            dest[len++] = *p++;
        }
    }
    dest[len] = '\0';
    return p;
}

// Get file size in sectors
static int get_file_size_in_sectors(const char* file_path) {
    FILE* fp = Psyz_FileOpen(file_path, "rb");
    if (!fp) {
        WARNF("Failed to open file for size calculation: %s", file_path);
        return 0;
    }

    int64_t file_size =
        Psyz_FileSeek(fp, 0, SEEK_END) == 0 ? Psyz_FileTell(fp) : -1;
    fclose(fp);

    if (file_size < 0) {
        WARNF("Failed to get file size: %s", file_path);
        return 0;
    }

    // Convert bytes to sectors (rounded up)
    return (int)((file_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
}

// Parse a CUE file and populate the global track table
static int parse_cue_file(const char* cue_path) {
    // Binary mode: lines end in LF or CRLF on every host (str_trim drops the
    // CR), and the paths are UTF-8 (Psyz_FileOpen).
    FILE* fp = Psyz_FileOpen(cue_path, "rb");
    if (!fp) {
        ERRORF("error opening CUE file %s", cue_path);
        return -1;
    }

    // Extract base directory from CUE path for resolving relative file paths:
    // the last separator of either kind ("C:/discs\\rr4.cue" mixes them).
    const char* last_slash = strrchr(cue_path, '/');
    const char* last_backslash = strrchr(cue_path, '\\');
    if (!last_slash || (last_backslash && last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
    if (last_slash) {
        unsigned int len = (unsigned int)(last_slash - cue_path + 1);
        if (len >= MAX_PATH_LEN) {
            len = MAX_PATH_LEN - 1;
        }
        strncpy(g_cue_base_path, cue_path, len);
        g_cue_base_path[len] = '\0';
    } else {
        g_cue_base_path[0] = '\0';
    }

    char full_line[1024];
    char current_file[MAX_PATH_LEN] = {0};
    char current_file_full_path[MAX_PATH_LEN] = {0};
    int current_track = -1;
    int current_file_abs_sector =
        0; // Absolute sector where current file starts

    g_track_count = 0;
    int first_line = 1;
    while (fgets(full_line, sizeof(full_line), fp)) {
        char* line = full_line;
        // A UTF-8 byte order mark, as Windows editors save CUE sheets.
        if (first_line && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
            line += 3;
        }
        first_line = 0;
        line = str_trim(line);
        if (strncmp(line, "FILE", 4) == 0) {
            // Before switching to new file, calculate size of previous file
            if (current_file[0] != '\0') {
                int prev_file_size =
                    get_file_size_in_sectors(current_file_full_path);
                current_file_abs_sector += prev_file_size;
            }

            char* p = line + 4;
            extract_token(p, current_file, MAX_PATH_LEN);

            // Build full path for the new file
            if (current_file[0] == '/' || current_file[0] == '\\' ||
                (current_file[0] != '\0' && current_file[1] == ':')) {
                // Absolute path
                strncpy(current_file_full_path, current_file, MAX_PATH_LEN - 1);
            } else {
                // Relative path - prepend base directory
                snprintf(current_file_full_path, MAX_PATH_LEN, "%s%s",
                         g_cue_base_path, current_file);
            }
        } else if (strncmp(line, "TRACK", 5) == 0) {
            int track_num = 1;
            char type_str[64];
            if (sscanf(line, "TRACK %d %63s", &track_num, type_str) == 2) {
                if (track_num < 1 || track_num > MAX_TRACKS) {
                    ERRORF("Invalid track number: %d", track_num);
                    continue;
                }

                current_track = track_num - 1;
                TrackEntry* track = &g_tracks[current_track];
                track->track_num = track_num;
                track->is_valid = 1;
                track->start_sector = 0; // Will be set by INDEX 01
                track->abs_sector =
                    0; // Will be calculated when INDEX 01 is parsed

                // Store file path
                strncpy(
                    track->file_path, current_file_full_path, MAX_PATH_LEN - 1);

                if (strcmp(type_str, "AUDIO") == 0) {
                    track->type = TRACK_AUDIO;
                } else if (strcmp(type_str, "MODE1/2352") == 0) {
                    track->type = TRACK_MODE1_2352;
                } else if (strcmp(type_str, "MODE2/2352") == 0) {
                    track->type = TRACK_MODE2_2352;
                } else {
                    track->type = TRACK_UNKNOWN;
                    DEBUGF("Unknown track type: %s", type_str);
                }

                if (track_num > g_track_count) {
                    g_track_count = track_num;
                }
            }
        } else if (strncmp(line, "INDEX", 5) == 0) {
            if (current_track < 0) {
                ERRORF("INDEX found before TRACK");
                continue;
            }
            int index_num;
            char msf_str[32];
            if (sscanf(line, "INDEX %d %31s", &index_num, msf_str) == 2) {
                int minute = 0, second = 0, frame = 0;
                if (sscanf(msf_str, "%d:%d:%d", &minute, &second, &frame) ==
                    3) {
                    int sector = minute * 60 * 75 + second * 75 + frame;
                    if (index_num == 1) { // INDEX 01 is the track data
                        TrackEntry* track = &g_tracks[current_track];
                        track->start_sector = sector;
                        track->abs_sector = current_file_abs_sector + sector;
                        DEBUGF("Track %02d: %s at sector %d", track->track_num,
                               track->file_path, track->abs_sector);
                    }
                }
            }
        } else {
            WARNF("CUE line not recognized: %s", line);
        }
    }
    fclose(fp);

    if (g_track_count == 0) {
        ERRORF("No valid tracks found in CUE file");
        return -1;
    }
    DEBUGF("Parsed CUE file: %d tracks", g_track_count);
    return 0;
}

// Number of bytes CD_cw writes for the short status-style responses.
#define CD_STATUS_RESULT_LEN 4

// Cached last-command response, exposed via CD_sync().
static u_char last_result[8];
static size_t last_result_len = 0;
static int last_intr = CdlComplete;

// `result` points at a caller-owned buffer sized for its own command, which is
// usually shorter than last_result, so only `len` bytes may be touched.
static void cache_last_result(const u_char* result, size_t len, int intr) {
    if (len > sizeof(last_result)) {
        len = sizeof(last_result);
    }
    memset(last_result, 0, sizeof(last_result));
    memcpy(last_result, result, len);
    last_result_len = len;
    last_intr = intr;
}

static int is_disk_loaded = 0;
static u_char data_sector[SECTOR_SIZE];
static int data_read_active = 0;
static int data_sector_valid = 0;
static int data_sector_number = 0;
static int data_getsector_count = 0;

/* Drive timing. The controller acknowledges a command after about 0.74 ms;
 * seeks, pauses, stops and the reset take their mechanical time, and a read
 * delivers its first sector one sector period after the seek, then one per
 * period (75 or 150 per second, CdlModeSpeed). The model is DuckStation's
 * (cdrom.cpp: GetAckDelayForCommand, GetTicksForSeek, GetTicksForPause,
 * GetTicksForStop, GetTicksForSpeedChange), on the emulated clock
 * (Psyz_KernelNowUs): CdSync reports CdlNoIntr until the last command has
 * completed and blocking waits spend that time. Psyz_CdSetInstantTiming(1)
 * (or PSYZ_CD_TIMING=instant) drops the mechanical delays: commands
 * complete at once and reads deliver as fast as they are polled. */
#define DRIVE_CLOCK 33868800ull // system clock ticks per second
#define DRIVE_ACK_TICKS 25000
#define DRIVE_INIT_ACK_TICKS 80000
#define DRIVE_INIT_TICKS 4000000
static struct {
    int motor_on;
    uint64_t done_us;   // the last command completes (CdSync) at this time
    int sync_cb_due;    // CD_cbsync waits for done_us
    uint64_t speed_us;  // a speed change in progress ends here
    int setloc_pending; // a Setloc not yet used by a seek, read or play
    // Idle head: it holds near hold_lba, its subchannel position cycling
    // through the track before it (hold_subq at hold_us, one sector per
    // period). A seek starts from there.
    int hold_lba, hold_subq, hold_offset;
    uint64_t hold_us;
    // A read or play: seeks to read_lba, reaches it at seek_end_us, the
    // first sector arrives at read_first_us, one per period after it.
    int read_lba;
    uint64_t seek_end_us, read_first_us;
} drive = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int drive_instant = -1;
// The BIOS boots at double speed: the first CdInit slows the drive down.
static int drive_init_was_double = 1; // the speed CdInit finds

void Psyz_CdSetInstantTiming(int instant) { drive_instant = instant != 0; }

static int drive_is_instant(void) {
    if (drive_instant < 0) {
        const char* timing = getenv("PSYZ_CD_TIMING");
        drive_instant = timing && !strcmp(timing, "instant");
    }
    return drive_instant;
}

static uint64_t ticks_us(uint64_t ticks) {
    return (ticks * 1000000 + DRIVE_CLOCK / 2) / DRIVE_CLOCK;
}

static int drive_double_speed(void) { return (CD_mode & CdlModeSpeed) != 0; }

static uint64_t drive_sector_ticks(void) {
    return DRIVE_CLOCK / (drive_double_speed() ? 150 : 75);
}

static uint64_t drive_sector_us(void) { return ticks_us(drive_sector_ticks()); }

static uint64_t drive_rate(void) { return drive_double_speed() ? 150 : 75; }

// Sectors per revolution at a position (the 71 minute table).
static int drive_sectors_per_track(int lba) {
    static const unsigned char spt_end[][2] = {
        {0, 8},   {4, 9},   {7, 10},  {11, 11}, {16, 12}, {23, 13},
        {27, 14}, {32, 15}, {39, 16}, {44, 17}, {52, 18}, {60, 19},
        {67, 20}, {74, 21}, {255, 22}};
    int mm = lba > 0 ? lba / (75 * 60) : 0;
    for (int i = 0; i < LEN(spt_end); i++) {
        if (mm <= spt_end[i][0]) {
            return spt_end[i][1];
        }
    }
    return 22;
}

// Time for the head to reach `to` from `from`, including a spin-up and the
// rest of a speed change.
static uint64_t drive_seek_ticks(int from, int to) {
    uint64_t ticks = 0;
    if (!drive.motor_on) {
        ticks += DRIVE_CLOCK; // spin-up, 1 second
        from = 0;
    }
    unsigned diff = (unsigned)(to > from ? to - from : from - to);
    uint64_t per_sector = drive_sector_ticks();
    int spt = drive_sectors_per_track(from);
    int tjump = from >= spt ? from - spt : 0;
    if (from < to && diff <= (unsigned)spt) {
        // just ahead: the mech reads on until the sector comes up
        ticks += per_sector * (diff > 2 ? diff : 2);
    } else if (from >= to && tjump <= to) {
        // one track back
        unsigned n = (unsigned)(to - tjump);
        ticks += per_sector * (n > 1 ? n : 1);
    } else if (diff < 7200) {
        double minutes = (double)from / (75 * 60);
        minutes = minutes < 1 ? 1 : minutes > 72 ? 72 : minutes;
        unsigned switch_point = (unsigned)(330.0f + (-63.1333f * logf((float)minutes)));
        ticks += (uint64_t)((diff < switch_point ? 0.05f : 0.1f) * DRIVE_CLOCK);
    } else {
        // sled seek: about 0.2 s, up to 0.9 s across the whole disc
        const float fixed = 0.05f, variable = 0.9f - fixed, log_weight = 0.4f;
        const float max_lba = 72.0f * 75 * 60;
        float seconds = fixed +
                        variable * (logf((float)diff) / logf(max_lba)) * log_weight +
                        variable * ((float)diff / max_lba) * (1.0f - log_weight);
        ticks += (uint64_t)(seconds * DRIVE_CLOCK);
    }
    ticks += DRIVE_CLOCK * 3 / 4000; // 0.5-1 ms of servo settling
    return ticks;
}

static uint64_t drive_now(void) { return Psyz_KernelNowUs(); }

// Mechanical delays count from the acknowledge.
static uint64_t drive_ack_us(u_char com) {
    return ticks_us(com == CdlReset ? DRIVE_INIT_ACK_TICKS : DRIVE_ACK_TICKS);
}

/* Emulated time spent by a call that polls the drive in a busy loop: the
 * PS1 time of the call and its loop (Psyz_SpendTime). */
static void drive_spend_ns(unsigned ns) {
    if (!drive_is_instant()) {
        Psyz_SpendTime(ns);
    }
}

static void* drive_bell(void) {
    static void* bell;
    if (!bell) {
        bell = Psyz_OsBellCreate();
    }
    return bell;
}

// Blocks until emulated time `until_us`, as a PS1 waiting on the drive.
static void drive_wait_until(uint64_t until_us) {
    if (drive_is_instant()) {
        return;
    }
    uint64_t now = drive_now();
    if (until_us <= now) {
        return;
    }
    if (Psyz_KernelOfflineClock()) {
        Psyz_AudioWaitUs(until_us - now);
        return;
    }
    for (;;) {
        Psyz_KernelPoll();
        /* One reading: the poll may itself pass until_us. */
        uint64_t now_us = drive_now();
        if (now_us >= until_us) {
            break;
        }
        uint64_t left = until_us - now_us;
        if (left > 2000) {
            Psyz_OsBellWait(drive_bell(), (uint32_t)(left - 1000));
        }
    }
}

// Compute the absolute sector of the disc lead-out, used by CdlGetTD with
// param = 0x00. Equals the absolute sector where the last track's file ends.
static int lead_out_sector(void) {
    if (g_track_count == 0) {
        return 0;
    }
    TrackEntry* last = &g_tracks[g_track_count - 1];
    return last->abs_sector + get_file_size_in_sectors(last->file_path);
}

// 1 = disc lid is open, 0 = lid closed
static int shell_is_open = 0;
void Psyz_CdShellOpen(int is_open) {
    shell_is_open = is_open ? 1 : 0;
    if (shell_is_open) {
        CD_status |= CdlStatShellOpen;
        CD_status &= ~CdlStatStandby;
    } else {
        CD_status &= ~CdlStatShellOpen;
        CD_status |= CdlStatStandby;
    }
}

static void close_track_file(void);

static int read_raw_sector(int sector, u_char* out);
static void drive_hold(int lba, int subq, int offset, uint64_t at);

/* The BIOS hands over with the head just past the executable it read: the
 * first sector of user data starting "PS-X EXE" within the first minute,
 * plus its text size. */
static void drive_boot_position(void) {
    u_char sector[SECTOR_SIZE];
    for (int lba = 16; lba < 75 * 60; lba++) {
        if (read_raw_sector(lba, sector) < 0) {
            return;
        }
        if (!memcmp(sector + 24, "PS-X EXE", 8)) {
            u32 size = (u32)sector[24 + 0x1C] | (u32)sector[24 + 0x1D] << 8 |
                       (u32)sector[24 + 0x1E] << 16 |
                       (u32)sector[24 + 0x1F] << 24;
            int end = lba + 1 + (int)((size + 2047) / 2048);
            drive_hold(end, end, 2, 0);
            return;
        }
    }
}

int Psyz_CdSetDiskPath(const char* diskPath) {
#if PE_NATIVE_STATE_CPU_COMPONENTS
    extern void Psyz_StateCdChanged(void);Psyz_StateCdChanged();
#endif
    close_track_file();
    is_disk_loaded = 0;
    g_track_count = 0;
    memset(g_tracks, 0, sizeof(g_tracks));
    if (!diskPath) {
        DEBUGF("Disk path unset");
        return 0;
    }
    if (parse_cue_file(diskPath) < 0) {
        return -1;
    }
    is_disk_loaded = 1;
    DEBUGF("Disk path set: %s", diskPath);
    drive_boot_position();
    return 0;
}

static PsyzCdReadCB disk_read_cb = NULL;
void Psyz_CdSetReadCB(PsyzCdReadCB cb) { disk_read_cb = cb; }

#define N_CHANNELS 2
#define SAMPLE_SIZE sizeof(short)
#define SECTOR_SIZE 2352
#define BUFFER_SECTORS 8 // Read 8 sectors at a time
#define CD_BUF_FRAMES                                                          \
    (SECTOR_SIZE * BUFFER_SECTORS / (N_CHANNELS * SAMPLE_SIZE))
static FILE* track_file;
static const TrackEntry* track_file_track;

static int read_sector_unlocked(int sector, u_char* out);

static void close_track_file(void) {
    if (track_file) {
        fclose(track_file);
        track_file = NULL;
    }
    track_file_track = NULL;
}
static s16 cd_buf[CD_BUF_FRAMES * N_CHANNELS];
static size_t cd_buf_pos = 0;   // current read position (in frames)
static size_t cd_buf_count = 0; // number of valid frames in buffer
static int is_playing = 0; // pause or unpause seeking through the CD stream
static int is_muted = 0;   // return empty samples while CD keeps streaming
// Silence before the drive's audio starts: the seek of a play or XA read.
static uint64_t cd_delay_frames;

// Emits the silence still due before CD audio starts; returns its frames.
static size_t cd_pull_delay(short* buf, size_t max_frames) {
    size_t n = cd_delay_frames < max_frames ? (size_t)cd_delay_frames : max_frames;
    memset(buf, 0, n * N_CHANNELS * SAMPLE_SIZE);
    cd_delay_frames -= n;
    return n;
}

static void cd_data_end(void) {
    if (CD_cbready) {
        CD_cbready(CdlDataEnd, NULL);
    }
    Psyz_KernelRaise(HwCdRom, EvSpTRAP);
}

// CD audio pull callback — called by SPU when its internal ring buffer
// runs low. Fills `buf` with up to `max_frames` interleaved stereo frames.
// One mutex lock per batch rather than per frame.
static size_t cdda_pull_samples(short* buf, size_t max_frames) {
    size_t written = 0;
    int hit_eof = 0;
    Psyz_AudioLock();
    if (!is_playing || !track_file) {
        goto end;
    }
    written = cd_pull_delay(buf, max_frames);
    while (written < max_frames) {
        // Refill file read buffer if consumed
        if (cd_buf_pos >= cd_buf_count) {
            size_t nRead = fread(cd_buf, 1, sizeof(cd_buf), track_file);
            if (nRead > 0) {
                cd_buf_count = (int)(nRead / (N_CHANNELS * SAMPLE_SIZE));
                cd_buf_pos = 0;
            } else {
                DEBUGF("cd audio playback reached end of file");
                is_playing = 0;
                hit_eof = 1;
                break;
            }
        }
        // Copy as many frames as possible from file buffer to output
        const size_t avail = cd_buf_count - cd_buf_pos;
        const size_t want = max_frames - written;
        const size_t n = avail < want ? avail : want;
        if (is_muted) {
            memset(&buf[written * 2], 0, n * N_CHANNELS * SAMPLE_SIZE);
        } else {
            memcpy(&buf[written * 2], &cd_buf[cd_buf_pos * 2],
                   n * N_CHANNELS * SAMPLE_SIZE);
        }
        cd_buf_pos += n;
        written += n;
    }
end:
    Psyz_AudioUnlock();
    if (hit_eof) {
        Psyz_KernelPost(cd_data_end);
    }
    return written;
}

#define XA_DECODED_MAX_FRAMES 4032            // 18 blocks * 4 sub * 28 samples
#define XA_STEP_Q16 ((37800u << 16) / 44100u) // 56173
// Real hardware keeps reading sectors and discards the ones rejected by the
// filter, so once the selected file/channel ends playback goes silent. Give up
// after this many consecutive rejected sectors instead of scanning the disc
// for the next matching one, which is unrelated audio.
#define XA_MAX_SECTOR_GAP 64

static struct {
    short decoded[XA_DECODED_MAX_FRAMES * 2];
    int active;
    unsigned char filter_file;
    unsigned char filter_channel;
    int decoded_count;
    int decoded_pos;
    int hist_l_old, hist_l_older;
    int hist_r_old, hist_r_older;
    // Hermite ring: index [0]=oldest .. [3]=newest per channel
    short rh_l[4], rh_r[4];
    unsigned int phase; // 16.16 fractional position into the input stream
    int cur_abs_sector; // absolute last sector used, for CdlGetlocL
    int stream_ended;   // filtered file/channel ran out, emit silence
    int silent_sectors_pending;
} xa;

static void xa_reset_stream(void) {
    xa.decoded_count = 0;
    xa.decoded_pos = 0;
    xa.hist_l_old = xa.hist_l_older = 0;
    xa.hist_r_old = xa.hist_r_older = 0;
    memset(xa.rh_l, 0, sizeof(xa.rh_l));
    memset(xa.rh_r, 0, sizeof(xa.rh_r));
    xa.phase = 0;
    xa.stream_ended = 0;
    xa.silent_sectors_pending = 0;
}

static int xa_sector_matches(unsigned char file, unsigned char channel) {
    if (!(CD_mode & CdlModeSF)) {
        return 1;
    }
    return file == xa.filter_file && channel == xa.filter_channel;
}

static int s4(int n) { return (n & 0x8) ? (n - 0x10) : n; }

static const int xa_pos[4] = {0, 60, 115, 98};
static const int xa_neg[4] = {0, 0, -52, -55};
// Decode 28 samples for one (block, nibble) pair into `dst` (stride=2 stereo
// or stride=1 mono). Returns updated old/older via pointers.
static void xa_decode_28(const unsigned char* blk, int sub, int nibble,
                         short* dst, int stride, int* prev, int* prev2) {
    const unsigned char hdr = blk[4 + sub * 2 + nibble];
    const int shift_in = hdr & 0x0F;
    const int shift = (shift_in > 12) ? 9 : (12 - shift_in);
    const int filter = (hdr & 0x30) >> 4; // 0..3
    const int f0 = xa_pos[filter];
    const int f1 = xa_neg[filter];
    for (int j = 0; j < 28; j++) {
        const unsigned char byte = blk[16 + sub + j * 4];
        const int t = s4((byte >> (nibble * 4)) & 0x0F);
        const int s =
            t * (1 << shift) + (((*prev) * f0 + (*prev2) * f1 + 32) / 64);
        dst[j * stride] = clamp16(s);
        *prev2 = *prev;
        *prev = s;
    }
}

// Decode a Form2 user-data area (0x914 = 2324 bytes) of 4-bit stereo XA.
// Writes interleaved L,R s16 frames into xa.decoded; returns frame count.
static int xa_decode_user_stereo_4bit(const unsigned char* user) {
    int frame = 0;
    for (int i = 0; i < 18; i++) {
        const unsigned char* block = user + i * 128;
        for (int sub = 0; sub < 4; sub++) {
            xa_decode_28(block, sub, 0, &xa.decoded[(frame) * 2 + 0], 2,
                         &xa.hist_l_old, &xa.hist_l_older);
            xa_decode_28(block, sub, 1, &xa.decoded[(frame) * 2 + 1], 2,
                         &xa.hist_r_old, &xa.hist_r_older);
            frame += 28;
        }
    }
    return frame;
}

// Decode a Form2 user-data area of 4-bit mono XA: both nibble passes write
// to the same mono buffer; we duplicate to L and R for output.
static int xa_decode_user_mono_4bit(const unsigned char* user) {
    short mono[XA_DECODED_MAX_FRAMES];
    int frame = 0;
    for (int i = 0; i < 18; i++) {
        const unsigned char* block = user + i * 128;
        for (int sub = 0; sub < 4; sub++) {
            xa_decode_28(block, sub, 0, &mono[frame], 1, &xa.hist_l_old,
                         &xa.hist_l_older);
            frame += 28;
            xa_decode_28(block, sub, 1, &mono[frame], 1, &xa.hist_l_old,
                         &xa.hist_l_older);
            frame += 28;
        }
    }
    for (int i = 0; i < frame; i++) {
        xa.decoded[i * 2 + 0] = mono[i];
        xa.decoded[i * 2 + 1] = mono[i];
    }
    return frame;
}

static int xa_read_and_decode_sector(void) {
    unsigned char sector[SECTOR_SIZE];
    int gap = 0;
    /* Once the selected clip ends, keep consuming the physical image while
     * emitting silence. Otherwise GetlocL freezes and bounded image readers
     * never reach EOF. One silent 2016-frame block lasts 8 sectors at 2x
     * (4 at 1x). Do not decode unrelated clips after the filter gap. */
    int silent_sectors = (CD_mode & CdlModeSpeed) ? 8 : 4;
    if (xa.silent_sectors_pending) {
        xa.silent_sectors_pending =
            xa.silent_sectors_pending > silent_sectors
                ? xa.silent_sectors_pending - silent_sectors
                : 0;
        goto silence;
    }
    while (!xa.stream_ended || silent_sectors-- > 0) {
        // Seek every sector: data reads of the same image (streaming) share
        // track_file with this decoder.
        const size_t n =
            disk_read_cb ? (size_t)disk_read_cb(&(struct PsyzCdRead){
                               (unsigned int)(xa.cur_abs_sector + 1),
                               SECTOR_SIZE, sector})
            : read_sector_unlocked(xa.cur_abs_sector + 1, sector) == 0
                ? SECTOR_SIZE
                : 0;
        if (n != SECTOR_SIZE) {
            return 0; // EOF or short read
        }
        xa.cur_abs_sector++;
        if (xa.stream_ended)
            continue;
        const unsigned char file = sector[0x10];
        const unsigned char channel = sector[0x11];
        const unsigned char submode = sector[0x12];
        const unsigned char ci = sector[0x13];
        if ((submode & 0x44) != 0x44 || !xa_sector_matches(file, channel)) {
            // data sector or filtered out, skip from XA stream
            if (++gap >= XA_MAX_SECTOR_GAP) {
                xa.stream_ended = 1;
                /* The search already consumed these sectors. Account for
                 * their elapsed silence before reading the rest of the tail. */
                xa.silent_sectors_pending = gap - silent_sectors;
                goto silence;
            }
            continue;
        }
        const unsigned char* user = &sector[0x18];
        int stereo = (ci & 0x03) == 1;
        int rate18900 = (ci & 0x04) != 0;
        int bits8 = (ci & 0x30) != 0;
        if (rate18900 || bits8) {
            LOG_ONCE(
                "XA: 18900Hz or 8-bit not supported, decoding as 4bit/37800");
        }
        if (stereo) {
            xa.decoded_count = xa_decode_user_stereo_4bit(user);
        } else {
            xa.decoded_count = xa_decode_user_mono_4bit(user);
        }
        xa.decoded_pos = 0;
        return 1;
    }
silence:
    memset(xa.decoded, 0, sizeof(xa.decoded));
    xa.decoded_count = XA_DECODED_MAX_FRAMES / 2;
    xa.decoded_pos = 0;
    return 1;
}

// Pull next 37800 Hz stereo input frame into the Hermite ring buffers.
// Returns 1 on success, 0 if stream ended.
static int xa_advance_input(void) {
    if (xa.decoded_pos >= xa.decoded_count) {
        if (!xa_read_and_decode_sector()) {
            return 0;
        }
    }
    short l = xa.decoded[xa.decoded_pos * 2 + 0];
    short r = xa.decoded[xa.decoded_pos * 2 + 1];
    xa.decoded_pos++;
    xa.rh_l[0] = xa.rh_l[1];
    xa.rh_l[1] = xa.rh_l[2];
    xa.rh_l[2] = xa.rh_l[3];
    xa.rh_l[3] = l;
    xa.rh_r[0] = xa.rh_r[1];
    xa.rh_r[1] = xa.rh_r[2];
    xa.rh_r[2] = xa.rh_r[3];
    xa.rh_r[3] = r;
    return 1;
}

// 4-point Hermite (Catmull-Rom-like) at fractional position frac in [0,1).
// frac is given as Q1.16 (only low 16 bits used).
// TODO !!! slown down considerably on hardware without float processing unit
static short hermite4(
    short ym1, short y0, short y1, short y2, unsigned int frac_q16) {
    // Use floating math here for correctness; this is run-once per output
    // sample and is still very cheap. Embedded targets get clean output.
    float x = (float)(frac_q16 & 0xFFFF) / 65536.0f;
    float c0 = (float)y0;
    float c1 = 0.5f * (float)(y1 - ym1);
    float c2 =
        (float)ym1 - 2.5f * (float)y0 + 2.0f * (float)y1 - 0.5f * (float)y2;
    float c3 = 0.5f * (float)(y2 - ym1) + 1.5f * (float)(y0 - y1);
    /* Contract each Horner stage explicitly: GCC and Clang otherwise choose
     * different multiply/add contractions near half-sample boundaries. */
    float v = fmaf(fmaf(fmaf(c3, x, c2), x, c1), x, c0);
    int iv = (int)(v + (v >= 0 ? 0.5f : -0.5f));
    return clamp16(iv);
}

static size_t xa_pull_samples(short* buf, size_t max_frames) {
    size_t written = 0;
    int hit_eof = 0;
    Psyz_AudioLock();
    if (!is_playing || (!track_file && !disk_read_cb) || !xa.active) {
        goto end;
    }
    written = cd_pull_delay(buf, max_frames);
    if (written == max_frames) {
        goto end;
    }
    // Prime Hermite ring on first call so we have y0..y2 valid.
    while (xa.rh_l[3] == 0 && xa.rh_l[2] == 0 && xa.rh_l[1] == 0 &&
           xa.decoded_pos == 0 && xa.decoded_count == 0) {
        if (!xa_advance_input()) {
            hit_eof = 1;
            goto end;
        }
        if (xa.decoded_count > 0)
            break;
    }
    while (written < max_frames) {
        // Advance input as many times as needed to get phase < 1.0.
        while (xa.phase >= 0x10000) {
            if (!xa_advance_input()) {
                hit_eof = 1;
                goto end;
            }
            xa.phase -= 0x10000;
        }
        if (is_muted) {
            buf[written * 2 + 0] = 0;
            buf[written * 2 + 1] = 0;
        } else {
            buf[written * 2 + 0] = hermite4(
                xa.rh_l[0], xa.rh_l[1], xa.rh_l[2], xa.rh_l[3], xa.phase);
            buf[written * 2 + 1] = hermite4(
                xa.rh_r[0], xa.rh_r[1], xa.rh_r[2], xa.rh_r[3], xa.phase);
        }
        xa.phase += XA_STEP_Q16;
        written++;
    }
end:
    if (hit_eof) {
        is_playing = 0;
        xa.active = 0;
        if (xa.cur_abs_sector >= 0)
            CdIntToPos(xa.cur_abs_sector, &CD_pos);
    }
    Psyz_AudioUnlock();
    if (hit_eof) {
        Psyz_KernelPost(cd_data_end);
    }
    return written;
}

size_t Psyz_CdPullSamples(short* out, size_t num_frames) {
    size_t read_frames;
    Psyz_AudioLock();
    u_char mode = CD_mode;
    Psyz_AudioUnlock();
    if (mode & CdlModeDA) {
        read_frames = cdda_pull_samples(out, num_frames);
    } else if (mode & CdlModeRT) {
        read_frames = xa_pull_samples(out, num_frames);
    } else {
        read_frames = 0;
    }
    if (!enable_passthrough) {
        for (size_t i = 0; i < read_frames; i++) {
            int l = out[i * 2 + 0];
            int r = out[i * 2 + 1];
            int l_out = (l * ctx.cd_src_l_dst_l + r * ctx.cd_src_r_dst_l) >> 7;
            int r_out = (l * ctx.cd_src_l_dst_r + r * ctx.cd_src_r_dst_r) >> 7;
            out[i * 2 + 0] = clamp16(l_out);
            out[i * 2 + 1] = clamp16(r_out);
        }
    }
    return read_frames;
}

// Open the track containing the absolute sector encoded in CD_pos. Closes
// any previously-open track_file. Returns 0 on success, -1 on failure.
static TrackEntry* find_track_for_sector(int sector) {
    for (int i = 0; i < g_track_count; i++) {
        if (!g_tracks[i].is_valid) {
            continue;
        }
        int next_abs_sector = INT_MAX;
        if (i + 1 < g_track_count && g_tracks[i + 1].is_valid) {
            next_abs_sector = g_tracks[i + 1].abs_sector;
        }
        if (sector >= g_tracks[i].abs_sector && sector < next_abs_sector) {
            return &g_tracks[i];
        }
    }
    return NULL;
}

static int open_track_at_cd_pos(void) {
    int sector = CdPosToInt(&CD_pos);
    TrackEntry* track = find_track_for_sector(sector);
    if (!track) {
        WARNF("no track found for sector %d", sector);
        return -1;
    }
    Psyz_AudioLock();
    close_track_file();
    FILE* file = Psyz_FileOpen(track->file_path, "rb");
    if (!file) {
        ERRORF("failed to open audio file: %s", track->file_path);
        Psyz_AudioUnlock();
        return -1;
    }
    int sector_offset = sector - track->abs_sector;
    sector_offset += track->start_sector;
    int64_t byte_offset = (int64_t)sector_offset * SECTOR_SIZE;
    if (Psyz_FileSeek(file, byte_offset, SEEK_SET) != 0) {
        ERRORF("failed to seek to sector %d in %s", sector, track->file_path);
        fclose(file);
        Psyz_AudioUnlock();
        return -1;
    }
    DEBUGF("opened track %s at sector %d (offset %d)", track->file_path, sector,
           sector_offset);
    track_file = file;
    track_file_track = track;
    Psyz_AudioUnlock();
    return 0;
}

// Reads one raw sector through track_file. The caller holds the audio lock.
static int read_sector_unlocked(int sector, u_char* out) {
    TrackEntry* track = find_track_for_sector(sector);
    if (!track) {
        return -1;
    }
    if (track != track_file_track || !track_file) {
        close_track_file();
        track_file = Psyz_FileOpen(track->file_path, "rb");
        if (!track_file) {
            ERRORF("failed to open data file: %s", track->file_path);
            return -1;
        }
        track_file_track = track;
    }
    int64_t offset =
        (int64_t)(sector - track->abs_sector + track->start_sector) *
        SECTOR_SIZE;
    if (Psyz_FileSeek(track_file, offset, SEEK_SET) == 0 &&
        fread(out, 1, SECTOR_SIZE, track_file) == SECTOR_SIZE) {
        return 0;
    }
    return -1;
}

static int read_raw_sector(int sector, u_char* out) {
    Psyz_AudioLock();
    int result = read_sector_unlocked(sector, out);
    Psyz_AudioUnlock();
    return result;
}

static int read_data_sector_into(int sector, u_char* out);
static uint64_t drive_start_us; // sector n (from 1) arrives n periods later
static uint64_t drive_delivered;

/* Drive position and command timing (see "Drive timing" above). */
static int drive_reading(void) {
    return data_read_active || xa.active || is_playing;
}

// Last sector the current read or play has reached by `now`; -1 before
// its first sector.
static int drive_read_position(uint64_t now) {
    if (now < drive.read_first_us) {
        return -1;
    }
    uint64_t n = (now - drive.read_first_us) * drive_rate() / 1000000;
    return drive.read_lba + (int)n;
}

static void drive_hold(int lba, int subq, int offset, uint64_t at) {
    drive.hold_lba = lba;
    drive.hold_subq = subq < 0 ? 0 : subq;
    drive.hold_offset = offset;
    drive.hold_us = at;
}

// Subchannel position of the idle head at `now`: it jumps back a track
// whenever it passes the hold position.
static int drive_hold_subq(uint64_t now) {
    int spt = drive_sectors_per_track(drive.hold_lba);
    int hold = drive.hold_lba + drive.hold_offset;
    int tjump = hold >= spt ? hold - spt : 0;
    int64_t passed =
        now > drive.hold_us
            ? (int64_t)((now - drive.hold_us) * drive_rate() / 1000000)
            : 0;
    int64_t offset = (int64_t)(drive.hold_subq - tjump) + passed;
    offset %= spt;
    if (offset < 0) {
        offset += spt;
    }
    return tjump + (int)offset;
}

// Where a seek issued at `now` starts from.
static int drive_seek_from(uint64_t now) {
    if (!drive.motor_on) {
        return 0;
    }
    if (now < drive.seek_end_us) {
        return drive.read_lba; // seeking: from its target
    }
    if (drive_reading()) {
        int lba = drive_read_position(now);
        return lba < 0 ? drive.read_lba : lba;
    }
    return drive_hold_subq(now);
}

// Moves the head to `to` from the acknowledge at `ack_us`, after a speed
// change in progress; returns when it arrives.
static uint64_t drive_seek(uint64_t ack_us, int to) {
    uint64_t end = ack_us;
    if (!drive_is_instant()) {
        end = ack_us > drive.speed_us ? ack_us : drive.speed_us;
        end += ticks_us(drive_seek_ticks(drive_seek_from(ack_us), to));
    }
    drive.motor_on = 1;
    drive.setloc_pending = 0;
    drive.read_lba = to;
    drive.seek_end_us = end;
    return end;
}

// A read or play of `to`: the head seeks, the first sector arrives one
// period after it.
static void drive_begin_read(uint64_t ack_us, int to) {
    uint64_t end = drive_seek(ack_us, to);
    drive.read_first_us = end + (drive_is_instant() ? 0 : drive_sector_us());
}

// Time a Pause takes from its acknowledge.
static uint64_t drive_pause_ticks(uint64_t ack_us) {
    if (!drive_reading()) {
        return 27000;
    }
    int lba = drive_read_position(ack_us);
    int spt = drive_sectors_per_track(lba < 0 ? drive.read_lba : lba);
    int64_t per_sector = (int64_t)drive_sector_ticks();
    int64_t since = 0;
    if (lba >= 0) {
        uint64_t last_us = drive.read_first_us +
                           (uint64_t)(lba - drive.read_lba) * 1000000 / drive_rate();
        since = (int64_t)((ack_us - last_us) * DRIVE_CLOCK / 1000000);
    }
    int64_t reach = (spt - (data_read_active ? 2 : 0)) * per_sector - since;
    int64_t least = drive_double_speed() ? 1000000 : 2000000;
    return (uint64_t)(reach > least ? reach : least);
}

// The head stops reading at `ack_us`: a data read holds a track before the
// last sector it read.
static void drive_stop_reading(uint64_t ack_us) {
    if (!drive_reading()) {
        return;
    }
    int lba = drive_read_position(ack_us);
    if (lba < 0) {
        lba = drive.read_lba;
    }
    if (data_read_active) {
        int spt = drive_sectors_per_track(lba);
        drive_hold(lba, lba >= spt ? lba - spt : 0, 2, ack_us);
    } else {
        drive_hold(lba, lba, 0, ack_us);
    }
    drive.seek_end_us = 0;
}

/* The status byte follows the mechanism: Seek until a read or play reaches
 * its first sector, then Read (data, ADPCM) or Play (CD-DA); Standby while
 * the motor spins. */
static void drive_update_status(uint64_t now) {
    int st = CD_status & ~(CdlStatRead | CdlStatSeek | CdlStatPlay);
    if (drive.motor_on && !shell_is_open) {
        st |= CdlStatStandby;
    }
    if (drive_reading()) {
        if (!drive_is_instant() && now < drive.read_first_us) {
            st |= CdlStatSeek;
        } else {
            st |= (is_playing && !xa.active && !data_read_active)
                      ? CdlStatPlay
                      : CdlStatRead;
        }
    } else if (!drive_is_instant() && now < drive.seek_end_us) {
        st |= CdlStatSeek;
    }
    CD_status = st;
}

// Output frames of the time from now until `at_us`, at the SPU rate.
static uint64_t drive_frames_until(uint64_t at_us) {
    uint64_t now = drive_now();
    return at_us > now ? (at_us - now) * PSYZ_SPU_SAMPLE_RATE / 1000000 : 0;
}

// The first XA sector a read from `from` plays, counted from `from`.
static int xa_first_sector_offset(int from) {
    u_char sector[SECTOR_SIZE];
    for (int i = 0; i < XA_MAX_SECTOR_GAP; i++) {
        if (read_data_sector_into(from + i, sector) < 0) {
            break;
        }
        if ((sector[0x12] & 0x44) == 0x44 &&
            xa_sector_matches(sector[0x10], sector[0x11])) {
            return i;
        }
    }
    return 0;
}

static int need_cdda_rewind = 1;
// play on CDDA mode only (not XA)
static void psyz_play() {
    if (!(CD_mode & CdlModeDA)) {
        WARNF("CdlModeDA not active, audio will not be played");
        return;
    }
    if (is_playing) {
        DEBUGF("audio already playing");
        return;
    }
    data_read_active = 0;
    data_sector_valid = 0;
    if (!need_cdda_rewind) {
        Psyz_AudioLock();
        is_playing = 1;
        Psyz_AudioUnlock();
        return;
    }
    if (open_track_at_cd_pos() != 0) {
        return;
    }
    Psyz_AudioLock();
    cd_buf_pos = 0;
    cd_buf_count = 0;
    need_cdda_rewind = 0;
    is_playing = 1;
    Psyz_AudioUnlock();
}

// Begin XA streaming at CD_pos (CdlReadN / CdlReadS).
static void psyz_xa_read(void) {
    if (!(CD_mode & CdlModeRT)) {
        LOG_ONCE("CdlReadN/ReadS without CdlModeRT not implemented");
        return;
    }
    data_read_active = 0;
    data_sector_valid = 0;
    if (!disk_read_cb && open_track_at_cd_pos()) {
        return;
    }
    Psyz_AudioLock();
    xa_reset_stream();
    Psyz_SpuFlushCdSamples();
    xa.cur_abs_sector = CdPosToInt(&CD_pos) - 1;
    xa.active = 1;
    is_playing = 1;
    Psyz_AudioUnlock();
}

static void psyz_stop() {
    need_cdda_rewind = 1;
    Psyz_AudioLock();
    cd_delay_frames = 0;
    is_playing = 0;
    xa.active = 0;
    Psyz_SpuFlushCdSamples();
    data_read_active = 0;
    data_sector_valid = 0;
    close_track_file();
    cd_buf_pos = 0;
    cd_buf_count = 0;
    Psyz_AudioUnlock();
}

static void psyz_pause() {
    Psyz_AudioLock();
    cd_delay_frames = 0;
    is_playing = 0;
    xa.active = 0;
    Psyz_SpuFlushCdSamples();
    data_read_active = 0;
    data_sector_valid = 0;
    Psyz_AudioUnlock();
}

static void psyz_mute() {
    Psyz_AudioLock();
    is_muted = 1;
    Psyz_AudioUnlock();
}

static void psyz_demute() {
    Psyz_AudioLock();
    is_muted = 0;
    Psyz_AudioUnlock();
}

static void callback(void) { NOT_IMPLEMENTED; }

int CD_init(void) {
    u_char nop_result[8];
    CD_com = 0;
    if (CD_mode & CdlModeSpeed) {
        drive_init_was_double = 1;
    }
    CD_mode = 0;
    CD_cbready = 0;
    CD_cbsync = 0;
    CD_status1 = 0;
    CD_status = 0;
    ResetCallback();
    InterruptCallback(2, &callback);
    CD_cw(CdlNop, 0, nop_result, 0);
    if (CD_status & CdlStatShellOpen) {
        CD_cw(CdlNop, 0, nop_result, 0);
    }
    if (CD_cw(CdlReset, 0, 0, 0)) {
        return -1;
    }
    if (CD_cw(CdlDemute, 0, 0, 0)) {
        return -1;
    }
    if (CD_sync(0, 0) != 2) {
        return -1;
    }
    return 0;
}

void CD_initintr(void) { NOT_IMPLEMENTED; }
void CD_flush(void) { NOT_IMPLEMENTED; }

int CD_vol(CdlATV* vol) {
    // 0x1F801800 = 2
    ctx.cd_src_l_dst_l = vol->val0; // 0x1F801802
    ctx.cd_src_l_dst_r = vol->val1; // 0x1F801803

    // 0x1F801800 = 3
    ctx.cd_src_r_dst_r = vol->val2; // 0x1F801801
    ctx.cd_src_r_dst_l = vol->val3; // 0x1F801802
    CHNGATV();                      // 0x1F801803 = 0x20
    return 0;
}

int CD_getsector(void* madr, int size) {
    size_t bytes = (size_t)size * 4;
    if (!data_sector_valid || !madr)
        return -1;
    size_t offset = data_getsector_count++ == 0 ? 12u : 24u;
    if (offset + bytes > sizeof(data_sector))
        return -1;
    memcpy(madr, data_sector + offset, bytes);
    if (offset == 24u) {
        data_sector_valid = 0;
        data_sector_number++;
        data_getsector_count = 0;
    }
    return 0;
}
int CD_getsector2(void) {
    NOT_IMPLEMENTED;
    return 0; // LIBCD returns 0 once the DMA is started
}
void CD_datasync(int mode) { NOT_IMPLEMENTED; }

int CD_initvol(void) {
    CdlATV vol = {128, 0, 128, 0};
    if (SPUR(main_volx.left) == 0 && SPUR(main_volx.right) == 0) {
        SPUW(main_vol.left, 0x3FFF);
        SPUW(main_vol.right, 0x3FFF);
    }
    SPUW(cd_vol.left, 0x3FFF);
    SPUW(cd_vol.right, 0x3FFF);
    SPUW(spucnt, SPU_CTRL_MASK_SPU_ENABLE | SPU_CTRL_MASK_MUTE_SPU |
                     SPU_CTRL_MASK_CD_AUDIO_ENABLE);
    CD_vol(&vol);
    return 0;
}

/* CdSync: the state of the last command. A poll (mode 1) reports
 * CdlNoIntr until it completes; mode 0 waits for it. */
int CD_sync(int mode, u_char* result) {
    if (mode == 0) {
        drive_wait_until(drive.done_us);
        Psyz_CdServiceDrive();
    } else {
        Psyz_CdPollQuery();
        if (!drive_is_instant() && drive_now() < drive.done_us) {
            return CdlNoIntr;
        }
    }
    if (result) {
        // Hand back only what the last command produced; the caller sized its
        // buffer for that command, not for last_result.
        memcpy(result, last_result, last_result_len);
    }
    return last_intr;
}
static int read_data_sector_into(int sector, u_char* out) {
    if (disk_read_cb) {
        struct PsyzCdRead read = {(unsigned int)sector, SECTOR_SIZE, out};
        return disk_read_cb(&read) < 0 ? -1 : 0;
    }
    return read_raw_sector(sector, out);
}

static int read_data_sector(int sector) {
    return read_data_sector_into(sector, data_sector);
}

// XA audio sectors that the drive plays itself in CdlModeRT never reach
// the host. Subheader submode bit 2 marks an audio sector.
static int is_played_audio_sector(void) {
    return (CD_mode & CdlModeRT) && (data_sector[0x12] & 0x04);
}

// Polled reads (CdReady). A ready callback receives the sectors instead,
// and polled ADPCM playback (CdlModeRT) keeps this data path idle.
int CD_ready(int mode, u_char* result) {
    if (!data_read_active || CD_cbready || (CD_mode & CdlModeRT))
        return CdlNoIntr;
    if (!data_sector_valid) {
        // The next sector arrives one period after the previous one.
        uint64_t at = drive_start_us +
                      (drive_delivered + 1) * 1000000 / drive_rate();
        if (mode == 0) {
            drive_wait_until(at);
        } else {
            Psyz_CdPollQuery();
            if (!drive_is_instant() && drive_now() < at)
                return CdlNoIntr;
        }
        if (read_data_sector(data_sector_number) < 0)
            return CdlDiskError;
        drive_delivered++;
        data_sector_valid = 1;
        data_getsector_count = 0;
    }
    if (result) {
        result[0] = (u_char)CD_status;
    }
    return CdlDataReady;
}

// Frame being assembled by StCdInterrupt (below).
#define ST_SLOT_FILLING 3
#define ST_SLOT_BYTES 2016
static struct {
    int active;
    u32 frame;
    int slot, next, count;
} st_frame;

/* Reads deliver sectors at the drive's rate, 75 or 150 per second, on the
 * emulated clock (Psyz_KernelNowUs: the root counters' time, which follows
 * the offline sample clock, pauses and game speed): the first one sector
 * period after the seek (drive.read_first_us), so the data sectors stay in
 * step with the XA audio the mixer plays from the same stream. With a
 * ready callback (CdRead, CdRead2 streaming) each one raises DataReady on
 * it, which fetches it with CdGetSector; a sector it does not take is
 * lost, as on hardware. Sectors are delivered when the game services the
 * drive: every VSync and whenever it polls it (StGetNext, CdReadSync,
 * CdSync, CdReady), and each poll spends its PS1 time (drive_spend_ns), so
 * a busy wait sees them arrive as time passes. */
#define DRIVE_MAX_BURST 600 // sectors; older ones of a long stall are skipped
static unsigned drive_generation; // counts read commands
static int drive_servicing;
static int drive_ready_sector = -1; // sector the ready callback may fetch

/* PS1 time of one poll of the drive in a busy loop: StGetNext and its retry
 * loop in a movie player (Ridge Racer Type 4 spins 4,000,000 polls on an
 * empty ring before its first read: 5.83 s, 290 fields, on DuckStation
 * without breakpoints, which slow its CPU), and the other drive queries. */
#define ST_POLL_NS 1458
#define CD_POLL_NS 2000

// Sectors the current read has reached by `now` (0 while seeking).
static uint64_t drive_sectors_due(uint64_t now) {
    if (now < drive_start_us) {
        return 0;
    }
    return (now - drive_start_us) * drive_rate() / 1000000;
}

static void start_data_read(void) {
    data_sector_number = CdPosToInt(&CD_pos);
    data_sector_valid = 0;
    data_getsector_count = 0;
    data_read_active = 1;
    drive_start_us = drive.read_first_us > drive_sector_us()
                         ? drive.read_first_us - drive_sector_us()
                         : 0;
    drive_delivered = 0;
    drive_generation++;
    memset(&st_frame, 0, sizeof(st_frame));
    CD_status |= CdlStatRead;
}

static void deliver_data_sector(void) {
    int sector = data_sector_number;
    drive_delivered++;
    if (read_data_sector(sector) < 0) {
        data_read_active = 0;
        CD_status &= ~CdlStatRead;
        if (CD_cbready) {
            CD_cbready(CdlDataEnd, NULL);
        }
        return;
    }
    data_sector_number = sector + 1;
    if (is_played_audio_sector() || !CD_cbready) {
        return;
    }
    u_char result[8] = {(u_char)CD_status};
    // CdGetSector inside the callback reads this sector.
    data_sector_number = sector;
    data_sector_valid = 1;
    data_getsector_count = 0;
    drive_ready_sector = sector;
    unsigned generation = drive_generation;
    CD_cbready(CdlDataReady, result);
    drive_ready_sector = -1;
    if (generation == drive_generation) { // the callback issued no new read
        data_sector_valid = 0;
        data_getsector_count = 0;
        data_sector_number = sector + 1;
    }
}

// A command's completion raises CdSyncCallback once its time has come.
static void service_sync_callback(void) {
    if (!drive.sync_cb_due || drive_now() < drive.done_us) {
        return;
    }
    drive.sync_cb_due = 0;
    if (CD_cbsync) {
        u_char result[8];
        memcpy(result, last_result, sizeof(result));
        CD_cbsync((u_char)last_intr, result);
    }
}

static void drive_schedule(void);

void Psyz_CdServiceDrive(void) {
    if (drive_servicing) {
        return;
    }
    drive_servicing = 1;
    drive_update_status(drive_now());
    service_sync_callback();
    if (data_read_active && CD_cbready) {
        uint64_t due = drive_sectors_due(drive_now());
        if (drive_is_instant() && !(CD_mode & CdlModeRT)) {
            due = drive_delivered + DRIVE_MAX_BURST; // as fast as polled
        } else if (due > drive_delivered + DRIVE_MAX_BURST) {
            uint64_t skipped = due - drive_delivered - DRIVE_MAX_BURST;
            data_sector_number += (int)skipped;
            drive_delivered += skipped;
        }
        while (data_read_active && CD_cbready && drive_delivered < due) {
            unsigned generation = drive_generation;
            deliver_data_sector();
            if (generation != drive_generation) {
                break; // the callback restarted the read
            }
        }
    }
    drive_servicing = 0;
    drive_schedule();
}

/* The drive's interrupts come at their time: the next sector of a read
 * with a ready callback, a command's completion for CdSyncCallback. */
static void drive_timer(void) { Psyz_CdServiceDrive(); }

static void drive_schedule(void) {
    Psyz_KernelCancel(drive_timer);
    uint64_t next = 0;
    if (data_read_active && CD_cbready && !drive_is_instant()) {
        next = drive_start_us +
               ((drive_delivered + 1) * 1000000 + drive_rate() - 1) / drive_rate();
    }
    if (drive.sync_cb_due && CD_cbsync &&
        (!next || drive.done_us < next)) {
        next = drive.done_us;
    }
    if (next) {
        Psyz_KernelPostAt(next, drive_timer);
    }
}

static void drive_poll(unsigned ns) {
    drive_spend_ns(ns);
    Psyz_CdServiceDrive();
}

void Psyz_CdPollDrive(void) { drive_poll(ST_POLL_NS); }

void Psyz_CdPollQuery(void) { drive_poll(CD_POLL_NS); }

/* StCdInterrupt: the ready-callback half of LIBCD streaming. It moves the
 * current sector into the ring StSetRing installed: StRingSize 32-byte
 * StHEADERs followed by as many 2016-byte (504 word) payload slots. A sector
 * of a movie frame carries its StHEADER in the first 32 bytes of the Mode 2
 * user data and 2016 bytes of frame data after it; the nSectors sectors of a
 * frame fill consecutive slots so the frame is contiguous. Slot ids: 0 free,
 * 1 wrap to slot 0, 2 frame ready, 4 frame held by StGetNext (StFreeRing
 * clears it), ST_SLOT_FILLING while a frame is assembled. When the last
 * sector of a frame lands, the CD DMA completion callback (CdRead2 installs
 * data_ready_callback) publishes it. A frame that does not fit in the free
 * slots, or misses a sector, is dropped, as on hardware. */

static int st_slots_free(int start, int count) {
    for (int i = start; i < start + count; i++) {
        if (StRingAddr[i].id != 0) {
            return 0;
        }
    }
    return 1;
}

static void st_drop_frame(void) {
    if (st_frame.active) {
        for (int i = 0; i < st_frame.next; i++) {
            StRingAddr[st_frame.slot + i].id = 0;
        }
    }
    st_frame.active = 0;
}

// A new frame starts: reserve its slots, wrapping to slot 0 when the rest
// of the ring cannot hold it. Leaves a free slot at the end of the ring, so
// the read index never passes the last header.
static int st_begin_frame(const StHEADER* h) {
    int count = h->nSectors;
    if (count <= 0 || count >= StRingSize) {
        return 0;
    }
    if (StSTART_FLAG && (h->frameCount < (u32)StStartFrame ||
                         h->frameCount > (u32)StEndFrame)) {
        return 0;
    }
    int pos = StRingIdx2;
    if (pos < 0 || pos >= StRingSize) {
        pos = 0;
    }
    int wrap = pos + count >= StRingSize;
    int start = wrap ? 0 : pos;
    if ((wrap && StRingAddr[pos].id != 0) || !st_slots_free(start, count)) {
        return 0; // ring full: the decoder is behind, skip this frame
    }
    if (wrap && pos != 0) {
        StRingAddr[pos].id = 1;
    }
    StRingIdx2 = start;
    st_frame.active = 1;
    st_frame.frame = h->frameCount;
    st_frame.slot = start;
    st_frame.next = 0;
    st_frame.count = count;
    return 1;
}

void StCdInterrupt(void) {
    StHEADER h;
    if (drive_ready_sector < 0 || !StRingAddr || StRingSize < 2) {
        return; // DataEnd/DiskError or no ring: nothing to store
    }
    StCdIntrFlag = 0;
    if (data_sector[0x12] & 0x04) {
        return; // XA audio interleaved with the frames
    }
    memcpy(&h, data_sector + 24, sizeof(h));
    if (h.secCount == 0) {
        st_drop_frame(); // previous frame lost a sector
        if (!st_begin_frame(&h)) {
            return;
        }
    } else if (!st_frame.active || h.frameCount != st_frame.frame ||
               h.secCount != st_frame.next || h.nSectors != st_frame.count) {
        st_drop_frame();
        return;
    }
    int slot = st_frame.slot + h.secCount;
    StHEADER* dst = &StRingAddr[slot];
    u_char* payload =
        (u_char*)&StRingAddr[StRingSize] + (size_t)slot * ST_SLOT_BYTES;
    memcpy(payload, data_sector + 24 + sizeof(StHEADER), ST_SLOT_BYTES);
    *dst = h;
    dst->id = ST_SLOT_FILLING;
    CdIntToPos(drive_ready_sector, &dst->loc);
    Stsector_offset = (s16)h.secCount;
    if (++st_frame.next < st_frame.count) {
        return;
    }
    st_frame.active = 0;
    Stframe_no = (s32)h.frameCount;
    StRingIdx1 = st_frame.slot + st_frame.count;
    StFinalSector = 1;
    Psyz_DmaComplete(3); // data_ready_callback publishes the frame
    if (StFunc2 && StEndFrame && h.frameCount >= (u32)StEndFrame) {
        StFunc2();
    }
}

static int CD_command(u_char com, u_char* param, u_char* result, s32 arg3);

/* The acknowledge of the command being issued, which mechanical delays
 * count from. CD_cw waits for it, as libcd does, unless arg3 asks for no
 * wait (CdControlF). LIBCD's CD_cw, CD_sync and getintr (the matching
 * decompilation in ridge-racer-type-4-decomp) set the model: CdSync(1) is
 * CdlNoIntr until the Complete interrupt of a command with a second
 * response, or the acknowledge of one without; an error acknowledge makes
 * it CdlDiskError and the command fail. */
static uint64_t command_ack_us;

int CD_cw(u_char com, u_char* param, u_char* result, s32 arg3) {
    // LIBCD's CD_cw first waits (CD_sync(0)) for the previous command to
    // complete, a second response included.
    drive_wait_until(drive.done_us);
    Psyz_CdServiceDrive();
    CD_com = com; // CdLastCom
    command_ack_us = drive_now();
    if (!drive_is_instant()) {
        command_ack_us += drive_ack_us(com);
    }
    drive.done_us = command_ack_us;
    drive.sync_cb_due = 1;
    drive_update_status(drive_now());
    int ret = CD_command(com, param, result, arg3);
    static int trace = -1;
    if (trace < 0) {
        trace = getenv("PSYZ_CD_TRACE") != NULL;
    }
    if (trace) {
        // emulated time, command, Setloc position, completion, read timing
        fprintf(stderr,
                "cd %" PRIu64 " %s pos %d ret %d done %" PRIu64
                " seek_end %" PRIu64 " first %" PRIu64 "\n",
                drive_now(), com < LEN(CD_comstr) ? CD_comstr[com] : "?",
                CdPosToInt(&CD_pos), ret, drive.done_us, drive.seek_end_us,
                drive.read_first_us);
    }
    Psyz_KernelRaise(HwCdRom, EvSpTRAP);
    drive_schedule();
    if (!arg3) {
        drive_wait_until(command_ack_us);
    }
    return ret;
}

static int CD_command(u_char com, u_char* param, u_char* result, s32 arg3) {
    int total;
    int t;
    int abs_sector;

    uint64_t ack = command_ack_us;
    switch (com) {
    case CdlNop:
        if (shell_is_open) {
            if (result) {
                result[0] = (u_char)(CD_status | CdlStatError);
                result[1] = 0x80;
                result[2] = 0x00;
                result[3] = 0x00;
                cache_last_result(result, CD_STATUS_RESULT_LEN, CdlDiskError);
            }
            return -1;
        }
        if (result) {
            result[0] = (u_char)CD_status;
            result[1] = 0x00;
            result[2] = 0x00;
            result[3] = 0x00;
            cache_last_result(result, CD_STATUS_RESULT_LEN, CdlComplete);
        }
        break;
    case CdlSetloc:
        if (!param) {
            ERRORF("%s got NULL param", CD_comstr[com]);
            return -2;
        }
        CD_pos = *(CdlLOC*)param;
        drive.setloc_pending = 1;
        break;
    case CdlPlay:
        if (!is_playing || drive.setloc_pending) {
            int was_playing = is_playing;
            drive_begin_read(ack, CdPosToInt(&CD_pos));
            psyz_play();
            if (is_playing && !was_playing) {
                Psyz_AudioLock();
                cd_delay_frames = drive_frames_until(drive.read_first_us);
                Psyz_AudioUnlock();
                CD_status |= CdlStatPlay;
            }
        }
        break;
    case CdlReadN:
    case CdlReadS:
        // With CdlModeRT the drive plays XA audio sectors itself and hands
        // the remaining (data) sectors to the host, as interleaved movie
        // streams need; without it every sector is data.
        // Already reading there: the drive carries on.
        if (data_read_active &&
            (!drive.setloc_pending ||
             CdPosToInt(&CD_pos) == drive_read_position(ack) + 1)) {
            drive.setloc_pending = 0;
            break;
        }
        drive_begin_read(ack, CdPosToInt(&CD_pos));
        if (CD_mode & CdlModeRT) {
            int first = drive_is_instant()
                            ? 0
                            : xa_first_sector_offset(CdPosToInt(&CD_pos));
            psyz_xa_read();
            Psyz_AudioLock();
            cd_delay_frames = drive_frames_until(
                drive.read_first_us +
                (uint64_t)first * 1000000 / drive_rate());
            Psyz_AudioUnlock();
        } else {
            Psyz_AudioLock();
            is_playing = 0;
            xa.active = 0;
            need_cdda_rewind = 1;
            Psyz_AudioUnlock();
        }
        start_data_read();
        break;
    case CdlSetfilter:
        if (!param) {
            ERRORF("%s got NULL param", CD_comstr[com]);
            return -2;
        }
        xa.filter_file = param[0];
        xa.filter_channel = param[1];
        break;
    case CdlStop: {
        uint64_t ticks = !drive.motor_on      ? 7000
                         : drive_double_speed() ? 25000000
                                                : 13000000;
        drive_stop_reading(ack);
        psyz_stop();
        drive.motor_on = 0;
        drive.seek_end_us = 0;
        CD_status &= ~(CdlStatRead | CdlStatSeek | CdlStatPlay | CdlStatStandby);
        if (!drive_is_instant()) {
            drive.done_us = ack + ticks_us(ticks);
        }
        break;
    }
    case CdlPause: {
        // The mech rejects a pause before a read or play reaches its first
        // sector.
        if (!drive_is_instant() && drive_reading() &&
            ack < drive.read_first_us) {
            u_char error[2] = {(u_char)(CD_status | CdlStatError), 0x80};
            if (result) {
                memcpy(result, error, sizeof(error));
            }
            cache_last_result(error, sizeof(error), CdlDiskError);
            return -1;
        }
        uint64_t ticks = drive_pause_ticks(ack);
        drive_stop_reading(ack);
        psyz_pause();
        CD_status &= ~(CdlStatRead | CdlStatSeek | CdlStatPlay);
        if (!drive_is_instant()) {
            drive.done_us = ack + ticks_us(ticks);
        }
        break;
    }
    case CdlSeekL:
    case CdlSeekP: {
        // The head moves to the Setloc position and holds there; logical
        // seeks complete on a data header 2 sectors before the target.
        int target = CdPosToInt(&CD_pos);
        drive_stop_reading(ack);
        psyz_pause();
        CD_status &= ~(CdlStatRead | CdlStatPlay);
        uint64_t end = drive_seek(ack, target);
        drive.seek_end_us = end;
        drive.read_first_us = end;
        if (com == CdlSeekL) {
            drive_hold(target, target - 2, 2, end);
        } else {
            drive_hold(target, target, 0, end);
        }
        drive.done_us = end;
        break;
    }
    case CdlStandby:
        if (!drive.motor_on && !drive_is_instant()) {
            drive.done_us = ack + ticks_us(400000);
            drive.speed_us = ack + ticks_us(DRIVE_CLOCK); // spin-up
        }
        drive.motor_on = 1;
        CD_status |= CdlStatStandby;
        break;
    case CdlReset: {
        // Init: the head returns to the start of the disc, the drive to
        // single speed. A Setmode while it settles costs no speed change.
        uint64_t ticks = DRIVE_INIT_TICKS;
        if (drive.motor_on) {
            uint64_t seek = drive_init_was_double
                                ? (uint64_t)(0.7 * DRIVE_CLOCK)
                                : 0;
            if (drive_seek_from(ack) != 0) {
                seek += drive_seek_ticks(drive_seek_from(ack), 0);
            }
            ticks = seek > ticks ? seek : ticks;
        } else {
            ticks += DRIVE_CLOCK;
        }
        drive_init_was_double = 0;
        drive_stop_reading(ack);
        psyz_pause();
        drive.motor_on = 1;
        drive.seek_end_us = 0;
        drive.setloc_pending = 0;
        CD_status &= ~(CdlStatRead | CdlStatSeek | CdlStatPlay);
        CD_status |= CdlStatStandby;
        if (!drive_is_instant()) {
            drive.done_us = ack + ticks_us(ticks);
        }
        drive_hold(0, 0, 0, drive.done_us);
        drive.speed_us = drive.done_us;
        break;
    }
    case CdlMute:
        psyz_mute();
        break;
    case CdlDemute:
        psyz_demute();
        break;
    case CdlSetmode:
        if (!param) {
            ERRORF("%s got NULL param", CD_comstr[com]);
            return -2;
        }
        if (*param & CdlModeDA) {
            Psyz_AudioInit();
        }
        if (*param & CdlModeRT) {
            Psyz_AudioInit();
        }
        if (*param & CdlModeAP) {
            LOG_ONCE("%s does not support CdlModeAP", CD_comstr[com]);
        }
        if (*param & CdlModeRept) {
            LOG_ONCE("%s does not support CdlModeRept", CD_comstr[com]);
        }
        if (*param & CdlModeSize0) {
            LOG_ONCE("%s does not support CdlModeSize0", CD_comstr[com]);
        }
        if (*param & CdlModeSize1) {
            LOG_ONCE("%s does not support CdlModeSize1", CD_comstr[com]);
        }
        // CdlModeSpeed sets the data rate of callback-driven reads. The
        // CdlModeStream bits never reach the drive: CdRead2 keeps them in
        // StMode and installs the stream callbacks.
        if (((*param ^ CD_mode) & CdlModeSpeed) && drive.motor_on &&
            !drive_is_instant()) {
            uint64_t change = ticks_us(
                (uint64_t)(((*param & CdlModeSpeed) ? 0.6 : 0.7) * DRIVE_CLOCK));
            if (drive.speed_us > ack) {
                // a change still in progress is cancelled early on
                if (drive.speed_us - ack >= change / 4) {
                    drive.speed_us = 0;
                }
            } else if (drive_reading() || ack < drive.seek_end_us) {
                // the read or seek in progress is delayed by the change
                drive.seek_end_us += change;
                drive.read_first_us += change;
                drive_start_us += change;
            } else {
                drive.speed_us = ack + change;
            }
        }
        Psyz_AudioLock();
        CD_mode = *param;
        Psyz_AudioUnlock();
        break;
    case CdlGetlocL: {
        if (!result) {
            ERRORF("%s got NULL result", CD_comstr[com]);
            return -2;
        }
        // always return XA sector if XA playback is active
        Psyz_AudioLock();
        int cur = xa.active ? xa.cur_abs_sector : CdPosToInt(&CD_pos);
        if (cur < 0) {
            cur = 0;
        }
        CdlLOC loc;
        CdIntToPos(cur, &loc);
        result[0] = loc.minute;
        result[1] = loc.second;
        result[2] = loc.sector;
        result[3] = 0x01;
        result[4] = xa.filter_file;
        result[5] = xa.filter_channel;
        result[6] = 0x00;
        result[7] = 0x00;
        cache_last_result(result, 8, CdlComplete);
        Psyz_AudioUnlock();
        break;
    }
    case CdlGetlocP: {
        // Subchannel Q position: track, index, position within the track and
        // absolute position (MSF, BCD). The result may be fetched later with
        // CdSync, so a NULL buffer is allowed.
        u_char loc_result[8];
        Psyz_AudioLock();
        int cur = xa.active          ? xa.cur_abs_sector
                  : data_read_active ? data_sector_number
                                     : CdPosToInt(&CD_pos);
        Psyz_AudioUnlock();
        if (cur < 0) {
            cur = 0;
        }
        TrackEntry* track = find_track_for_sector(cur);
        int rel = track ? cur - track->abs_sector : cur;
        int track_num = track ? track->track_num : 1;
        CdlLOC abs_loc;
        CdIntToPos(cur, &abs_loc);
        loc_result[0] = (u_char)(((track_num / 10) << 4) | (track_num % 10));
        loc_result[1] = 0x01;
        loc_result[2] = (u_char)((((rel / 4500) / 10) << 4) | ((rel / 4500) % 10));
        loc_result[3] = (u_char)(((((rel / 75) % 60) / 10) << 4) | ((rel / 75) % 60 % 10));
        loc_result[4] = (u_char)((((rel % 75) / 10) << 4) | (rel % 75 % 10));
        loc_result[5] = abs_loc.minute;
        loc_result[6] = abs_loc.second;
        loc_result[7] = abs_loc.sector;
        if (result) {
            memcpy(result, loc_result, sizeof(loc_result));
        }
        cache_last_result(loc_result, sizeof(loc_result), CdlComplete);
        break;
    }
    case CdlGetTN:
        if (!result) {
            ERRORF("%s got NULL result", CD_comstr[com]);
            return -2;
        }
        if (shell_is_open || !is_disk_loaded || g_track_count == 0) {
            result[0] = (u_char)(CD_status | CdlStatError);
            result[1] = 0x80; // error-response sentinel
            result[2] = 0x00;
            result[3] = 0x00;
            cache_last_result(result, CD_STATUS_RESULT_LEN, CdlDiskError);
            return -1;
        }
        result[0] = (u_char)CD_status;
        result[1] = itob(1);
        result[2] = itob(g_track_count);
        result[3] = 0x00;
        cache_last_result(result, CD_STATUS_RESULT_LEN, CdlComplete);
        break;
    case CdlGetTD:
        if (!param || !result) {
            ERRORF("%s got NULL param/result", CD_comstr[com]);
            return -2;
        }
        if (shell_is_open || !is_disk_loaded || g_track_count == 0) {
            result[0] = (u_char)(CD_status | CdlStatError);
            result[1] = 0x80;
            result[2] = 0x00;
            result[3] = 0x00;
            cache_last_result(result, CD_STATUS_RESULT_LEN, CdlDiskError);
            return -1;
        }
        t = btoi(param[0]);
        abs_sector;
        if (t == 0) {
            abs_sector = lead_out_sector();
        } else if (t < 1 || t > g_track_count || !g_tracks[t - 1].is_valid) {
            result[0] = (u_char)(CD_status | CdlStatError);
            result[1] = 0x80;
            result[2] = 0x00;
            result[3] = 0x00;
            cache_last_result(result, CD_STATUS_RESULT_LEN, CdlDiskError);
            return -1;
        } else {
            abs_sector = g_tracks[t - 1].abs_sector;
        }
        // The +150 lead-in pregap is part of the controller-reported MSF.
        total = abs_sector + 150;
        result[0] = (u_char)CD_status;
        result[1] = itob(total / 75 / 60);
        result[2] = itob(total / 75 % 60);
        result[3] = 0x00;
        cache_last_result(result, CD_STATUS_RESULT_LEN, CdlComplete);
        break;
    default:
        if (com >= LEN(CD_comstr)) {
            ERRORF("com %X invalid", com);
            return -1;
        }
        LOG_ONCE("com %s not implemented", CD_comstr[com]);
    }
    return 0;
}

// LIBCD's: up to five attempts at one Init with the volume reset. (LIBCD
// then installs callbacks delivering HwCdRom events; PsyZ raises those from
// CD_cw, and a ready callback would take the sectors of polled reads.)
int CdInit(void) {
    for (int i = 0; i < 5; i++) {
        if (CD_init() == 0 && CD_initvol() == 0) {
            return 1;
        }
    }
    return 0;
}

int CdReading() {
    NOT_IMPLEMENTED;
    return 0;
}

void ExecCd() { NOT_IMPLEMENTED; }

/* CdRead, CdReadSync and CdReadCallback are LIBCD's (decomp cdread.c): a
 * ready callback that takes each sector the drive delivers. */

/* Native save states (PE_NATIVE_STATE_CPU_COMPONENTS, built by Parasite Eve
 * PC only; Ridge Racer Type 4 does not define it) keep their fixed 79584-byte
 * PECD001 layout: the drive timing (struct drive, command_ack_us, the
 * delivery timeline, timed interrupts) is not in it. A capture requires an
 * idle data path; a restore leaves the head holding where it is with no
 * command pending, so at most a seek's time differs after it. */
#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
#include <stdint.h>
#include <limits.h>
static uint64_t state_cd_generation=1;
void Psyz_StateCdChanged(void){if(++state_cd_generation==0)state_cd_generation=1;}
static CdlCB state_cd_sync,state_cd_ready;static PsyzCdReadCB state_cd_reader;static int state_cd_bound;
static void state_cd_put(unsigned char *o,uint32_t v){for(unsigned i=0;i<4;i++)o[i]=(unsigned char)(v>>(8*i));}
static uint32_t state_cd_get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int state_cd_canonical(uint32_t v,unsigned k){switch(k){case 1:return v<=255;case 2:return v<=1;case 3:return v<=8;case 4:return v<=CD_BUF_FRAMES;case 5:return v<=XA_DECODED_MAX_FRAMES;case 6:return(int32_t)v>=-32768&&(int32_t)v<=32767;default:return 1;}}
static int state_cd_walk(const unsigned char *in,unsigned char *out,int apply){size_t at=32;
#define FIELD(value,kind) do{if(out){state_cd_put(out+at,(uint32_t)(value));}else{uint32_t v=state_cd_get(in+at);if(!state_cd_canonical(v,kind))return 0;if(apply)(value)=(__typeof__(value))v;}at+=4;}while(0)
FIELD(CD_cbread,0);
FIELD(CD_debug,0);
FIELD(CD_status,0);
FIELD(CD_status1,0);
FIELD(CD_nopen,0);
FIELD(CD_mode,1);
FIELD(CD_com,1);
FIELD(DS_active,0);
FIELD(last_result_len,3);
FIELD(last_intr,0);
FIELD(is_disk_loaded,2);
FIELD(data_read_active,2);
FIELD(data_sector_valid,2);
FIELD(data_sector_number,0);
FIELD(data_getsector_count,0);
FIELD(shell_is_open,2);
FIELD(cd_buf_pos,4);
FIELD(cd_buf_count,4);
FIELD(is_playing,2);
FIELD(is_muted,2);
FIELD(need_cdda_rewind,2);
FIELD(enable_passthrough,2);
FIELD(xa.active,2);
FIELD(xa.filter_file,1);
FIELD(xa.filter_channel,1);
FIELD(xa.decoded_count,5);
FIELD(xa.decoded_pos,5);
FIELD(xa.hist_l_old,0);
FIELD(xa.hist_l_older,0);
FIELD(xa.hist_r_old,0);
FIELD(xa.hist_r_older,0);
FIELD(xa.phase,0);
FIELD(xa.cur_abs_sector,0);
FIELD(xa.stream_ended,2);
FIELD(xa.silent_sectors_pending,0);
FIELD(ctx.cd_src_l_dst_l,1);
FIELD(ctx.cd_src_l_dst_r,1);
FIELD(ctx.cd_src_r_dst_l,1);
FIELD(ctx.cd_src_r_dst_r,1);
FIELD(ctx.adpcm_mute,1);
FIELD(ctx.is_stereo,1);
FIELD(ctx.sample_rate,1);
FIELD(ctx.bits_per_sample,1);
FIELD(ctx.emphasis,1);
FIELD(CD_pos.minute,1);
FIELD(CD_pos.second,1);
FIELD(CD_pos.sector,1);
FIELD(CD_pos.track,1);
for(unsigned i=0;i<8;i++){FIELD(last_result[i],1);}
for(unsigned i=0;i<2352;i++){FIELD(data_sector[i],1);}
for(unsigned i=0;i<CD_BUF_FRAMES*2;i++){FIELD(cd_buf[i],6);}
for(unsigned i=0;i<XA_DECODED_MAX_FRAMES*2;i++){FIELD(xa.decoded[i],6);}
for(unsigned i=0;i<4;i++){FIELD(xa.rh_l[i],6);FIELD(xa.rh_r[i],6);}
#undef FIELD
return at==79584;}
size_t Psyz_StateCdBytes(void){return 79584;}
static int state_cd_bindings(void){return Psyz_StateAudioOwned()&&Psyz_StateKernelOwned()&&state_cd_bound&&state_cd_sync==CD_cbsync&&state_cd_ready==CD_cbready&&state_cd_reader==disk_read_cb;}
int Psyz_StateCdCapture(unsigned char *out,size_t bytes){if(!out||bytes!=79584||!Psyz_StateAudioOwned()||!Psyz_StateKernelOwned()||data_read_active)return 0;if(!state_cd_bound){state_cd_sync=CD_cbsync;state_cd_ready=CD_cbready;state_cd_reader=disk_read_cb;state_cd_bound=1;}if(!state_cd_bindings())return 0;uint32_t track=0;if(track_file){for(int i=0;i<g_track_count;i++)if(track_file_track==&g_tracks[i])track=i+1;if(!track)return 0;}int64_t pos=track_file?Psyz_FileTell(track_file):0;if(pos<0)return 0;memcpy(out,"PECD001!",8);state_cd_put(out+8,(uint32_t)state_cd_generation);state_cd_put(out+12,(uint32_t)(state_cd_generation>>32));state_cd_put(out+16,track);state_cd_put(out+20,(uint32_t)(uint64_t)pos);state_cd_put(out+24,(uint32_t)((uint64_t)pos>>32));state_cd_put(out+28,g_track_count);return state_cd_walk(0,out,0);}
int Psyz_StateCdValidate(const unsigned char *in,size_t bytes){if(!in||bytes!=79584||memcmp(in,"PECD001!",8)||!state_cd_bindings())return 0;uint64_t generation=(uint64_t)state_cd_get(in+8)|((uint64_t)state_cd_get(in+12)<<32),pos=(uint64_t)state_cd_get(in+20)|((uint64_t)state_cd_get(in+24)<<32);uint32_t track=state_cd_get(in+16);if(generation!=state_cd_generation||state_cd_get(in+28)!=(uint32_t)g_track_count||track>(uint32_t)g_track_count||pos>LONG_MAX||(!track&&pos)||data_read_active)return 0;if(track&&!g_tracks[track-1].is_valid)return 0;if(state_cd_get(in+76)||state_cd_get(in+96)>state_cd_get(in+100)||state_cd_get(in+136)>state_cd_get(in+132)||state_cd_get(in+156)>0x20000u||state_cd_get(in+168)>XA_MAX_SECTOR_GAP)return 0;return state_cd_walk(in,0,0);}
int Psyz_StateCdRestore(const unsigned char *in,size_t bytes){if(!Psyz_StateCdValidate(in,bytes))return 0;uint32_t track=state_cd_get(in+16);uint64_t pos=(uint64_t)state_cd_get(in+20)|((uint64_t)state_cd_get(in+24)<<32);FILE *next=0;if(track){next=Psyz_FileOpen(g_tracks[track-1].file_path,"rb");if(!next)return 0;if(Psyz_FileSeek(next,(int64_t)pos,SEEK_SET)){fclose(next);return 0;}}/* New validated handle exists before old one is released. */
FILE *previous=track_file;track_file=next;track_file_track=track?&g_tracks[track-1]:0;if(previous)fclose(previous);return state_cd_walk(in,0,1);}
#endif
