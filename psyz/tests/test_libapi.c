#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ztest.h"
#include <psyz.h>
#include <kernel.h>
#include <libapi.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#define PATH_SEP "\\"
#elif defined(__psx__)
// No host filesystem on the console; the groups using it skip on ps1.
#define mkdir(path, mode) (-1)
#define rmdir(path) (-1)
#define PATH_SEP "/"
#else
#include <sys/stat.h>
#define PATH_SEP "/"
#endif

// Fixture steps must run in release builds too, where NDEBUG removes the
// whole expression of an assert().
#define BU_CHECK(cond)                                                         \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: fixture step failed: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            abort();                                                           \
        }                                                                      \
    } while (0)

static void bu_make_file(const char* path, int len, int val, int inc) {
    FILE* f = fopen(path, "wb");
    BU_CHECK(f != NULL);
    unsigned char* d = (unsigned char*)malloc(len);
    BU_CHECK(d != NULL);
    for (int i = 0; i < len; i++) {
        d[i] = val;
        val += inc;
    }
    BU_CHECK(fwrite(d, 1, len, f) == (size_t)len);
    BU_CHECK(fclose(f) == 0);
    free(d);
}

ZTEST_SETUP(bu) {
    zskip_targets("ps1");
    mkdir("bu00", 0755);
    mkdir("bu10", 0755);
    bu_make_file("bu00/BASLUS-00000PSYZ00", 8192, 0, 1);
    bu_make_file("bu00/BASLUS-00000PSYZ01", 8192, 0, 2);
}

ZTEST_TEARDOWN(bu) {
    zskip_targets("ps1");
    BU_CHECK(remove("bu00/BASLUS-00000PSYZ00") == 0);
    BU_CHECK(remove("bu00/BASLUS-00000PSYZ01") == 0);
    BU_CHECK(rmdir("bu00") == 0);
    BU_CHECK(rmdir("bu10") == 0);
}

ZTEST(bu, for_invalid_path) {
    struct DIRENTRY d = {0};
    struct DIRENTRY* pd;
    pd = firstfile((char*)"none:", &d);
    zexpect_ptr_eq(NULL, pd);
    zexpect_str_eq("", d.name);
    zexpect_s32_eq(0, d.attr);
    zexpect_s32_eq(0, d.size);
}

ZTEST(bu, for_empty_path) {
    struct DIRENTRY d = {0};
    struct DIRENTRY* pd;
    pd = firstfile((char*)"bu10:", &d);
    zexpect_ptr_eq(NULL, pd);
    zexpect_str_eq("", d.name);
    zexpect_s32_eq(0, d.attr);
    zexpect_s32_eq(0, d.size);
}

ZTEST(bu, for_first_file) {
    struct DIRENTRY d = {0};
    struct DIRENTRY* pd;
    pd = firstfile((char*)"bu00:", &d);
    zexpect_ptr_eq(&d, pd);
    zexpect_str_eq("BASLUS-00000PSYZ00", d.name);
    zexpect_s32_eq(80, d.attr);
    zexpect_s32_eq(8192, d.size);
}

ZTEST(bu, for_multiple_files) {
    struct DIRENTRY d[2];
    struct DIRENTRY* pd;
    pd = nextfile(firstfile((char*)"bu00:", d) + 1);
    zexpect_ptr_eq(&d[1], pd);
    zexpect_str_eq("BASLUS-00000PSYZ01", pd->name);
    zexpect_s32_eq(80, pd->attr);
    zexpect_s32_eq(8192, pd->size);
}

ZTEST(bu, for_end_of_file_list) {
    struct DIRENTRY d[3];
    struct DIRENTRY* pd;
    pd = nextfile(nextfile(firstfile((char*)"bu00:", d) + 1) + 2);
    zexpect_ptr_eq(NULL, pd);
}

ZTEST(bu, open_existing_file) {
    int fd = open("bu00:BASLUS-00000PSYZ00", FREAD);
    zexpect_s32_ne(-1, fd);
    close(fd);
}

ZTEST(bu, create_and_write_file) {
    unsigned short expected = 1234, actual;
    int fd = open("bu00:BASLUS-00000PSYZ00", FCREAT);
    zexpect_s32_ne(-1, fd);
    write(fd, (char*)&expected, sizeof(unsigned short));
    close(fd);

    FILE* f = fopen("bu00/BASLUS-00000PSYZ00", "rb");
    int read = fread(&actual, 1, sizeof(unsigned short), f);
    fclose(f);
    zexpect_s32_eq(sizeof(unsigned short), read);
    zexpect_u16_eq(expected, actual);
}

ZTEST(bu, reopen_existing_file_for_write_and_read_write) {
    const char* name = "bu00:BASLUS-00000PSYZ00";
    const unsigned char replacement[] = {0x5A, 0xA5, 0x36, 0xC9};
    unsigned char actual[4] = {0};
    int fd = open(name, FWRITE);
    zassert_s32_ne(-1, fd);
    zexpect_s32_eq(4, write(fd, (char*)replacement, 4));
    close(fd);
    fd = open(name, FREAD | FWRITE);
    zassert_s32_ne(-1, fd);
    zexpect_s32_eq(4, read(fd, actual, 4));
    zexpect_u8array_eq(replacement, actual, 4);
    zexpect_s32_eq(0, lseek(fd, 0, SEEK_SET));
    zexpect_s32_eq(2, write(fd, (char*)replacement + 2, 2));
    close(fd);
    fd = open(name, FREAD);
    zassert_s32_ne(-1, fd);
    zexpect_s32_eq(4, read(fd, actual, 4));
    zexpect_u16_eq(0xC936, (unsigned)actual[0] | ((unsigned)actual[1] << 8));
    close(fd);
}

ZTEST(path_adjustment, basic_memory_card_path) {
    char dst[256] = {0};
    Psyz_AdjustPath(dst, "bu00:TESTFILE", sizeof(dst));
    zexpect_str_eq("bu00" PATH_SEP "TESTFILE", dst);
}

ZTEST(path_adjustment, basic_path_no_adjustment) {
    char dst[256] = {0};
    Psyz_AdjustPath(dst, "regular/path/file.txt", sizeof(dst));
    zexpect_str_eq("regular/path/file.txt", dst);
}

static int test_callback_override(char* dst, const char* src, int maxlen) {
    if (strncmp(src, "custom:", 7) == 0) {
        snprintf(dst, maxlen, "overridden/%s", src + 7);
        return (int)strlen(dst);
    }
    return -1;
}

ZTEST(path_adjustment, callback_override_positive_return) {
    char dst[256] = {0};
    Psyz_AdjustPathCB(test_callback_override);
    Psyz_AdjustPath(dst, "custom:myfile", sizeof(dst));
    zexpect_str_eq("overridden/myfile", dst);
    Psyz_AdjustPathCB(NULL);
}

ZTEST(path_adjustment, callback_negative_return_falls_back) {
    char dst[256] = {0};
    Psyz_AdjustPathCB(test_callback_override);
    Psyz_AdjustPath(dst, "bu00:FALLBACK", sizeof(dst));
    zexpect_str_eq("bu00" PATH_SEP "FALLBACK", dst);
    Psyz_AdjustPathCB(NULL);
}

ZTEST(path_adjustment, callback_null_uses_internal) {
    char dst[256] = {0};
    Psyz_AdjustPathCB(NULL);
    Psyz_AdjustPath(dst, "bu10:TESTFILE", sizeof(dst));
    zexpect_str_eq("bu10" PATH_SEP "TESTFILE", dst);
}

ZTEST(path_adjustment, preserves_twenty_byte_filenames) {
    char dst[256] = {0};
    Psyz_AdjustPath(dst, "bu00:1234567890123456789", sizeof(dst));
    zexpect_str_eq("bu00" PATH_SEP "1234567890123456789", dst);
    Psyz_AdjustPath(dst, "bu00:12345678901234567890", sizeof(dst));
    zexpect_str_eq("bu00" PATH_SEP "12345678901234567890", dst);
    Psyz_AdjustPath(dst, "bu00:123456789012345678901", sizeof(dst));
    zexpect_str_eq("bu00" PATH_SEP "12345678901234567890", dst);
}

static int test_callback_with_long_name(
    char* dst, const char* src, int maxlen) {
    if (strncmp(src, "custom:", 7) == 0) {
        snprintf(dst, maxlen, "custom_dir/%s", src + 7);
        return (int)strlen(dst);
    }
    return -1;
}

ZTEST(path_adjustment, callback_result_also_truncated) {
    char dst[256] = {0};
    Psyz_AdjustPathCB(test_callback_with_long_name);
    Psyz_AdjustPath(dst, "custom:VERYLONGFILENAME1234567890", sizeof(dst));
    zexpect_str_eq("custom_dir/VERYLONGFILENAME1234", dst);
    Psyz_AdjustPathCB(NULL);
}

#define LONG_FILENAME_21 "BISLUS-00005aaa195120"
#define TRUNCATED_NAME_20 "BISLUS-00005aaa19512"

ZTEST_SETUP(truncation) {
    zskip_targets("ps1");
    mkdir("bu00", 0755);
}

ZTEST_TEARDOWN(truncation) {
    zskip_targets("ps1");
    remove("bu00/BISLUS-00005aaa195120");
    remove("bu00/BISLUS-00005aaa19512");
    remove("bu00/AAAA-SHORT");
    rmdir("bu00");
}

ZTEST(truncation, open_truncates_long_filename) {
    FILE* f = fopen("bu00/BISLUS-00005aaa19512", "wb");
    zassert_ptr_ne(NULL, f);
    fclose(f);

    int fd = open("bu00:BISLUS-00005aaa195120", FREAD);
    zprintf("open() should truncate filename and find the 20-char file\n");
    zexpect_s32_ne(-1, fd);
    if (fd != -1) {
        close(fd);
    }
}

ZTEST(truncation, firstfile_truncates_long_filename) {
    FILE* f = fopen("bu00/BISLUS-00005aaa195120", "wb");
    zassert_ptr_ne(NULL, f);
    fclose(f);

    struct DIRENTRY d = {0};
    struct DIRENTRY* pd = firstfile((char*)"bu00:", &d);

    zprintf("firstfile() should find the file\n");
    zassert_ptr_ne(NULL, pd);
    zprintf("File name should be bounded to 20 bytes\n");
    zexpect_u8array_eq(TRUNCATED_NAME_20, d.name, 20);
}

ZTEST(truncation, nextfile_truncates_long_filename) {
    FILE* f1 = fopen("bu00/BISLUS-00005aaa195120", "wb");
    zassert_ptr_ne(NULL, f1);
    fclose(f1);

    FILE* f2 = fopen("bu00/AAAA-SHORT", "wb");
    zassert_ptr_ne(NULL, f2);
    fclose(f2);

    struct DIRENTRY d[2] = {0};
    struct DIRENTRY* pd;

    pd = firstfile((char*)"bu00:", &d[0]);
    zassert_ptr_ne(NULL, pd);
    zexpect_str_eq("AAAA-SHORT", d[0].name);

    pd = nextfile(&d[1]);
    zprintf("nextfile() should find the second file\n");
    zassert_ptr_ne(NULL, pd);
    zprintf("File name should be bounded to 20 bytes\n");
    zexpect_u8array_eq(TRUNCATED_NAME_20, d[1].name, 20);
}

#if defined(__psyz) && !defined(__PSP__)
#include <SDL3/SDL.h>

// Memory cards in a directory whose UTF-8 name is not ASCII, as a profile
// directory ("C:\Users\Zoë\...") gives it: save a file the way games do
// (create, write, reopen, seek, read back), list it with its size and erase
// it. The narrow CRT calls of the Windows backend failed all of these.
#define CARD_UTF8_DIR "psyz_card_\xc3\xab\xe6\x97\xa5"

static int card_utf8_path(char* dst, const char* src, int maxlen) {
    if (strncmp(src, "bu00:", 5) != 0) {
        return -1;
    }
    snprintf(dst, maxlen, CARD_UTF8_DIR "/%s", src + 5);
    return (int)strlen(dst);
}

ZTEST_SETUP(card_utf8) {
    SDL_CreateDirectory(CARD_UTF8_DIR);
    Psyz_AdjustPathCB(card_utf8_path);
}

ZTEST_TEARDOWN(card_utf8) {
    Psyz_AdjustPathCB(NULL);
    SDL_RemovePath(CARD_UTF8_DIR "/BESCES-00000RR4TEST");
    SDL_RemovePath(CARD_UTF8_DIR);
}

ZTEST(card_utf8, save_list_reload_and_erase) {
    static unsigned char block[8192], back[8192];
    const char* name = "bu00:BESCES-00000RR4TEST";
    for (int i = 0; i < (int)sizeof(block); i++) {
        block[i] = (unsigned char)(i * 7 + (i >> 8));
    }
    block[100] = 0x0A; // a text-mode write would expand these
    block[101] = 0x0D;
    block[102] = 0x1A; // and a text-mode read stop at this one

    int fd = open(name, FCREAT | (1 << 16));
    zassert_s32_ne(-1, fd);
    close(fd);
    fd = open(name, FWRITE);
    zassert_s32_ne(-1, fd);
    zexpect_s32_eq(8192, write(fd, (char*)block, 8192));
    close(fd);

    struct DIRENTRY d = {0};
    zassert_ptr_eq(&d, firstfile((char*)"bu00:", &d));
    zexpect_str_eq("BESCES-00000RR4TEST", d.name);
    zexpect_s32_eq(8192, d.size);

    fd = open(name, FREAD);
    zassert_s32_ne(-1, fd);
    zexpect_s32_eq(8192, read(fd, back, 8192));
    zexpect_u8array_eq(block, back, 8192);
    zexpect_s32_eq(4096, lseek(fd, 4096, SEEK_SET));
    zexpect_s32_eq(256, read(fd, back, 256));
    zexpect_u8array_eq(block + 4096, back, 256);
    zprintf("a negative SEEK_CUR offset moves back\n");
    zexpect_s32_eq(4096, lseek(fd, (unsigned int)-256, SEEK_CUR));
    close(fd);

    zexpect_s32_ne(0, erase((char*)name));
    zexpect_s32_eq(-1, open(name, FREAD));
}
#endif
