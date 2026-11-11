#include <string.h>
#include <psyz.h>
#include <libspu.h>
#include <libsnd.h>

#include "ztest.h"

#ifdef __psyz
#include <psyz/spu.h>

/* A one-program VAB (version 7, one tone on VAG 1 centred on note 60) and
 * SEQ data driven by SsSeqCalledTbyT without a timer (SS_NOTICK), checking
 * the SPU registers the PSY-Q 4.x sequencer writes. */
#define TEST_VAB 3
#define SPU_VAB_ADDR 0x10000
#define REG_VOLL(v) ((v) * 16 + 0)
#define REG_VOLR(v) ((v) * 16 + 2)
#define REG_PITCH(v) ((v) * 16 + 4)
#define REG_KON 0x188
#define REG_KOFF 0x18C

static unsigned char vab[32 + 128 * 16 + 16 * 32 + 256 * 2];
static unsigned char vag[16];
static char seq_table[SS_SEQ_TABSIZ];
static short marks[8];
static int mark_count;

static void put16(unsigned char* p, unsigned v) {
    p[0] = v;
    p[1] = v >> 8;
}

static void build_vab(void) {
    unsigned char* hdr = vab;
    unsigned char* prog = vab + 32;
    unsigned char* tone = prog + 128 * 16;
    unsigned char* vags = tone + 16 * 32;

    memset(vab, 0, sizeof(vab));
    memcpy(hdr, "pBAV", 4);
    hdr[4] = 7;           /* version */
    put16(hdr + 0x12, 1); /* programs */
    put16(hdr + 0x14, 1); /* tones */
    put16(hdr + 0x16, 1); /* VAGs */
    hdr[0x18] = 127;      /* master volume */
    hdr[0x19] = 64;       /* master pan */
    prog[0] = 1;          /* tones */
    prog[1] = 127;        /* volume */
    prog[4] = 64;         /* pan */
    tone[1] = 0;          /* mode: no reverb */
    tone[2] = 127;        /* volume */
    tone[3] = 64;         /* pan */
    tone[4] = 60;         /* centre note */
    tone[6] = 0;          /* key range */
    tone[7] = 127;
    tone[12] = 2; /* pitch bend range */
    tone[13] = 2;
    put16(tone + 16, 0x80FF); /* ADSR */
    put16(tone + 18, 0x5FC0);
    put16(tone + 22, 1); /* VAG 1 */
    put16(vags + 2, sizeof(vag) / 8);
    memset(vag, 0, sizeof(vag));
    vag[1] = 3; /* end + repeat */
}

static void on_mark(short access, short track, short data) {
    (void)access;
    (void)track;
    if (mark_count < 8) {
        marks[mark_count++] = data;
    }
}

static void snd_setup(void) {
    SsInit();
    SsSetTableSize(seq_table, 1, 1);
    SsSetTickMode(SS_NOTICK | SS_TICK60);
    build_vab();
    zassert_s32_eq(TEST_VAB, SsVabOpenHeadSticky(vab, TEST_VAB, SPU_VAB_ADDR));
    zassert_s32_eq(TEST_VAB, SsVabTransBody(vag, TEST_VAB));
    SsVabTransCompleted(SS_WAIT_COMPLETED);
    mark_count = 0;
}

/* SEQ header: resolution 48 ticks per beat at 60 bpm, so a 48-tick delta is
 * 60 calls of SsSeqCalledTbyT. */
static int seq_header(unsigned char* p) {
    static const unsigned char hdr[] = {
        'p', 'Q', 'E', 'S', 0, 0, 0, 1, 0, 48, 0x0F, 0x42, 0x40, 4, 2};
    memcpy(p, hdr, sizeof(hdr));
    return sizeof(hdr);
}

static unsigned kon_after_ticks(int ticks) {
    unsigned kon = 0;
    for (int i = 0; i < ticks; i++) {
        SsSeqCalledTbyT();
        kon |= Psyz_SpuRead(REG_KON) | (Psyz_SpuRead(REG_KON + 2) << 16);
    }
    return kon;
}

ZTEST_SETUP(libsnd) {
    zskip_targets("ps1;psp");
    Psyz_AudioPause();
    snd_setup();
}

ZTEST_TEARDOWN(libsnd) {
    SsUtAllKeyOff(0);
    Psyz_AudioUnpause();
}

static int lowest_voice(unsigned mask) {
    for (int v = 0; v < 24; v++) {
        if (mask & (1u << v)) {
            return v;
        }
    }
    return -1;
}

ZTEST(libsnd, pitch_follows_the_tone_centre) {
    zexpect_u16_eq(0x1000, SsPitchFromNote(60, 0, 60, 0));
    zexpect_u16_eq(0x2000, SsPitchFromNote(72, 0, 60, 0));
    zexpect_u16_eq(0x0800, SsPitchFromNote(48, 0, 60, 0));
    /* 0x879C * 0x8000 >> 16 = 0x43CE, rounded to the nearest 1/4 */
    zexpect_u16_eq(0x10F4, SsPitchFromNote(61, 0, 60, 0));
    zexpect_u16_eq(0x3FFF, SsPitchFromNote(90, 0, 60, 0));
}

ZTEST(libsnd, sequence_note_uses_squared_volume) {
    static unsigned char seq[64];
    int n = seq_header(seq);
    short access;

    seq[n++] = 0; /* delta */
    seq[n++] = 0x90, seq[n++] = 60, seq[n++] = 100, seq[n++] = 48;
    seq[n++] = 60, seq[n++] = 0, seq[n++] = 0; /* note off, delta 0 */
    seq[n++] = 0xFF, seq[n++] = 0x2F, seq[n++] = 0;
    access = SsSeqOpen((u_long*)seq, TEST_VAB);
    zassert_s32_eq(0, access);
    SsSeqPlay(access, SSPLAY_PLAY, 1);
    /* the tick reads the note; the flush of the next one keys it on */
    zexpect_u32_eq(0, kon_after_ticks(1));
    zexpect_u32_eq(1, kon_after_ticks(1));
    zexpect_u16_eq(0x1000, Psyz_SpuRead(REG_PITCH(0)));
    /* 100 * 0x3FFF * 127 / 127^2 = 12900, squared over 0x3FFF */
    zexpect_u16_eq(10157, Psyz_SpuRead(REG_VOLL(0)));
    zexpect_u16_eq(10157, Psyz_SpuRead(REG_VOLR(0)));
    /* the volume applies to the sounding voice at once */
    SsSeqSetVol(access, 64, 127);
    SsSeqCalledTbyT();
    zexpect_u16_eq(12900 * 64 / 127 * (12900 * 64 / 127) / 0x3FFF,
                   Psyz_SpuRead(REG_VOLL(0)));
    zexpect_u16_eq(10157, Psyz_SpuRead(REG_VOLR(0)));
    SsSeqStop(access);
    SsSeqCalledTbyT();
    SsSeqClose(access);
}

ZTEST(libsnd, sound_effect_volume_is_linear) {
    short voice = SsUtKeyOn(TEST_VAB, 0, 0, 60, 0, 100, 100);
    zassert_s32_ge(0, voice);
    SsSeqCalledTbyT();
    zexpect_u16_eq(12900, Psyz_SpuRead(REG_VOLL(voice)));
    zexpect_u16_eq(12900, Psyz_SpuRead(REG_VOLR(voice)));
    SsUtSetVVol(voice, 64, 32);
    SsSeqCalledTbyT();
    zexpect_u16_eq(64 * 0x81, Psyz_SpuRead(REG_VOLL(voice)));
    zexpect_u16_eq(32 * 0x81, Psyz_SpuRead(REG_VOLR(voice)));
}

ZTEST(libsnd, nrpn_loop_and_mark_callback) {
    static unsigned char seq[96];
    int n = seq_header(seq);
    int notes = 0;
    short access;

    seq[n++] = 0;
    seq[n++] = 0xB0, seq[n++] = 99, seq[n++] = 20, seq[n++] = 0; /* loop */
    seq[n++] = 6, seq[n++] = 3, seq[n++] = 0;   /* three times */
    seq[n++] = 99, seq[n++] = 40, seq[n++] = 0; /* mark */
    seq[n++] = 98, seq[n++] = 7, seq[n++] = 0;
    seq[n++] = 0x90, seq[n++] = 60, seq[n++] = 100, seq[n++] = 4;
    seq[n++] = 60, seq[n++] = 0, seq[n++] = 0;
    seq[n++] = 0xB0, seq[n++] = 99, seq[n++] = 30, seq[n++] = 0; /* loop end */
    seq[n++] = 0xFF, seq[n++] = 0x2F, seq[n++] = 0;
    access = SsSeqOpen((u_long*)seq, TEST_VAB);
    zassert_s32_eq(0, access);
    SsSetMarkCallback(access, 0, on_mark);
    SsSeqPlay(access, SSPLAY_PLAY, 1);
    for (int i = 0; i < 60; i++) {
        if (kon_after_ticks(1)) {
            notes++;
        }
    }
    zexpect_s32_eq(3, notes);
    zexpect_s32_eq(3, mark_count);
    zexpect_s32_eq(7, marks[0]);
    SsSeqClose(access);
}

ZTEST(libsnd, pause_stops_the_sequence_until_replay) {
    static unsigned char seq[64];
    int n = seq_header(seq);
    short access;
    int voice;

    seq[n++] = 0;
    seq[n++] = 0x90, seq[n++] = 60, seq[n++] = 100, seq[n++] = 48;
    seq[n++] = 62, seq[n++] = 100, seq[n++] = 48;
    seq[n++] = 0xFF, seq[n++] = 0x2F, seq[n++] = 0;
    access = SsSeqOpen((u_long*)seq, TEST_VAB);
    zassert_s32_eq(0, access);
    SsSeqPlay(access, SSPLAY_PLAY, 1);
    zexpect_u32_eq(1, kon_after_ticks(2));
    /* the pause tick keys the voices off, written by the next flush */
    SsSeqPause(access);
    SsSeqCalledTbyT();
    SsSeqCalledTbyT();
    zexpect_u16_eq(1, Psyz_SpuRead(REG_KOFF));
    zexpect_u32_eq(0, kon_after_ticks(120));
    SsSeqReplay(access);
    /* the second note was 58 calls away when paused */
    zexpect_u32_eq(0, kon_after_ticks(58));
    voice = lowest_voice(kon_after_ticks(1));
    zassert_s32_ge(0, voice);
    zexpect_u16_eq(
        SsPitchFromNote(62, 0, 60, 0), Psyz_SpuRead(REG_PITCH(voice)));
    SsSeqClose(access);
}

ZTEST(libsnd, unopened_access_number_is_ignored) {
    SsSeqSetVol(-1, 0, 0);
    SsSeqPause(-1);
    SsSeqReplay(-1);
    SsSeqStop(-1);
    SsSeqClose(-1);
    SsSetMarkCallback(-1, 0, on_mark);
    zexpect_s32_eq(0, mark_count);
}
#endif
