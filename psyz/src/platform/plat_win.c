#include <psyz.h>
#include <psyz/log.h>
#include <string.h>
#include <kernel.h>

// Undefine macros before including Windows headers to avoid conflicts
#undef open
#undef close
#undef lseek
#undef read
#undef write
#undef ioctl

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <direct.h>
#include <stdlib.h>
#include <share.h>
#include <SDL3/SDL.h>
#include <wchar.h>
#include <errno.h>
#include <stdio.h>
#include <romio.h>

/* SDL paths are UTF-8; the CRT's narrow file APIs take the ANSI code page,
 * so every host path goes through UTF-16. The path is made absolute and
 * normalised (_wfullpath resolves ".", ".." and "/") and given the
 * extended-length prefix, so user-data directories also work beyond
 * MAX_PATH. The result is released with SDL_free. */
static wchar_t* file_path_utf16(const char* path) {
    if (!path)
        return NULL;
    wchar_t* wide = (wchar_t*)SDL_iconv_string(
        "UTF-16LE", "UTF-8", path, strlen(path) + 1);
    if (!wide)
        return NULL;
    for (wchar_t* p = wide; *p; ++p)
        if (*p == L'/')
            *p = L'\\';
    if (!wcsncmp(wide, L"\\\\?\\", 4) || !*wide)
        return wide;
    // windows.h is not included (its names clash with the PS1 kernel.h);
    // _wfullpath is GetFullPathNameW through the CRT.
    wchar_t* resolved = _wfullpath(NULL, wide, 0);
    SDL_free(wide);
    if (!resolved)
        return NULL;
    size_t len = wcslen(resolved);
    // "\\?\UNC\" replaces the leading "\\" of a UNC path: 8 characters at most.
    wchar_t* full = (wchar_t*)SDL_malloc((len + 9) * sizeof(wchar_t));
    if (!full) {
        free(resolved);
        return NULL;
    }
    if (resolved[0] == L'\\' && resolved[1] == L'\\') {
        if (resolved[2] == L'?' || resolved[2] == L'.')
            memcpy(full, resolved, (len + 1) * sizeof(wchar_t));
        else {
            memcpy(full, L"\\\\?\\UNC", 7 * sizeof(wchar_t));
            memcpy(full + 7, resolved + 1, len * sizeof(wchar_t)); // "\server..."
        }
    } else {
        memcpy(full, L"\\\\?\\", 4 * sizeof(wchar_t));
        memcpy(full + 4, resolved, (len + 1) * sizeof(wchar_t));
    }
    free(resolved);
    return full;
}

FILE* Psyz_FileOpen(const char* path, const char* mode) {
    if (!path || !mode) {
        errno = EINVAL;
        return NULL;
    }
    wchar_t* wide_path = file_path_utf16(path);
    wchar_t* wide_mode = (wchar_t*)SDL_iconv_string(
        "UTF-16LE", "UTF-8", mode, strlen(mode) + 1);
    FILE* file = NULL;
    if (wide_path && wide_mode)
        file = _wfopen(wide_path, wide_mode);
    else
        errno = ENOMEM;
    SDL_free(wide_path);
    SDL_free(wide_mode);
    return file;
}

int Psyz_FileSeek(FILE* file, int64_t offset, int whence) {
    return _fseeki64(file, offset, whence);
}

int64_t Psyz_FileTell(FILE* file) { return _ftelli64(file); }

typedef struct {
    char** filenames;
    int file_count;
    int current_index;
    char base_dir[4096];
} WIN_DIRENTRY_RESERVED;

static WIN_DIRENTRY_RESERVED singleton_dir = {0};

static int compare_strings(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static void close_filesearch_handle() {
    if (singleton_dir.filenames) {
        for (int i = 0; i < singleton_dir.file_count; i++) {
            free(singleton_dir.filenames[i]);
        }
        free(singleton_dir.filenames);
        singleton_dir.filenames = NULL;
        singleton_dir.file_count = 0;
        singleton_dir.current_index = 0;
        singleton_dir.base_dir[0] = '\0';
    }
}

static int open_filesearch_handle(const char* basePath) {
    close_filesearch_handle();

    strncpy_s(singleton_dir.base_dir, sizeof(singleton_dir.base_dir), basePath,
              _TRUNCATE);

    // Build search pattern
    char search_pattern[4098];
    strncpy_s(search_pattern, sizeof(search_pattern), basePath,
              sizeof(search_pattern) - 3);

    // Ensure path ends with backslash
    size_t len = strlen(search_pattern);
    if (len > 0 && search_pattern[len - 1] != '\\' &&
        search_pattern[len - 1] != '/') {
        strcat_s(search_pattern, sizeof(search_pattern), "\\");
    }
    strcat_s(search_pattern, sizeof(search_pattern), "*");

    struct _wfinddata_t file_info;
    wchar_t* wide = file_path_utf16(search_pattern);
    if (!wide)
        return 0;
    intptr_t handle = _wfindfirst(wide, &file_info);
    SDL_free(wide);

    if (handle == -1) {
        return 0;
    }

    // Count and collect all regular files
    int capacity = 16;
    singleton_dir.filenames = (char**)malloc(capacity * sizeof(char*));
    singleton_dir.file_count = 0;
    if (!singleton_dir.filenames) {
        _findclose(handle);
        return 0;
    }

    do {
        // skip directories: ps1 memcards are flat
        // skip hidden files starting with `.`
        if (!(file_info.attrib & _A_SUBDIR) && file_info.name[0] != L'.') {
            if (singleton_dir.file_count >= capacity) {
                capacity *= 2;
                char** new_filenames = (char**)realloc(
                    (void*)singleton_dir.filenames, capacity * sizeof(char*));
                if (!new_filenames) {
                    close_filesearch_handle();
                    _findclose(handle);
                    return 0;
                }
                singleton_dir.filenames = new_filenames;
            }
            char* utf8 = SDL_iconv_string(
                "UTF-8", "UTF-16LE", (const char*)file_info.name,
                (wcslen(file_info.name) + 1) * sizeof(wchar_t));
            char* name = utf8 ? _strdup(utf8) : NULL;
            SDL_free(utf8);
            if (!name) {
                close_filesearch_handle();
                _findclose(handle);
                return 0;
            }
            singleton_dir.filenames[singleton_dir.file_count++] = name;
        }
    } while (!_wfindnext(handle, &file_info));

    _findclose(handle);

    if (singleton_dir.file_count == 0) {
        free(singleton_dir.filenames);
        singleton_dir.filenames = NULL;
        return 0;
    }

    // ensure directory entries are stored alphabetically
    qsort(singleton_dir.filenames, singleton_dir.file_count, sizeof(char*),
          compare_strings);

    singleton_dir.current_index = 0;
    return 1;
}

static void populate_entry(
    const char* baseDir, struct DIRENTRY* dst, const char* filename) {
    char buf[8192];
    SDL_PathInfo fileStat = {0};

    strncpy_s(buf, sizeof(buf), baseDir, _TRUNCATE);
    if (!Psyz_JoinPath(buf, filename, sizeof(buf))) {
        ERRORF("failed to join '%s' and '%s': strings are too large", baseDir,
               filename);
        return;
    }

    if (!SDL_GetPathInfo(buf, &fileStat)) {
        ERRORF("failed to stat '%s'", buf);
        return;
    }

    // ensure max 20 characters per name entry
    if (strlen(filename) > sizeof(dst->name)) {
        WARNF("'%s' will be truncated", filename);
    }
    strncpy(dst->name, filename, sizeof(dst->name));
    dst->attr = 0x10 | 0x40; // Same as Unix version
    dst->size = (long)fileStat.size;
    dst->next = NULL;
    dst->system[0] = 0;
}

struct DIRENTRY* my_firstfile(char* dirPath, struct DIRENTRY* firstEntry) {
    char basePath[4096];
    Psyz_AdjustPath(basePath, dirPath, sizeof(basePath));
    DEBUGF("opendir('%s')", basePath);

    if (!open_filesearch_handle(basePath)) {
        return NULL;
    }
    if (singleton_dir.current_index >= singleton_dir.file_count) {
        return NULL;
    }
    populate_entry(singleton_dir.base_dir, firstEntry,
                   singleton_dir.filenames[singleton_dir.current_index++]);
    return firstEntry;
}

struct DIRENTRY* my_nextfile(struct DIRENTRY* outEntry) {
    if (!outEntry || !singleton_dir.filenames) {
        return NULL;
    }
    if (singleton_dir.current_index >= singleton_dir.file_count) {
        close_filesearch_handle();
        return NULL;
    }
    populate_entry(singleton_dir.base_dir, outEntry,
                   singleton_dir.filenames[singleton_dir.current_index++]);
    return outEntry;
}

long my_erase(char* path) {
    char adjPath[4096];
    Psyz_AdjustPath(adjPath, path, sizeof(adjPath));

    DEBUGF("remove('%s')", adjPath);
    return SDL_RemovePath(adjPath);
}

long my_format(char* fs) {
    NOT_IMPLEMENTED;
    return 0;
}

int psyz_open(const char* devname, int flag, ...) {
    int oflag = _O_RDONLY;
    if (flag & FCREAT) {
        // fix: implies FWRITE, avoid creating 0kb unwritable files
        flag |= FWRITE;
    }
    if ((flag & (FREAD | FWRITE)) == (FREAD | FWRITE)) {
        oflag = _O_RDWR;
    } else if (flag & FREAD) {
        oflag = _O_RDONLY;
    } else if (flag & FWRITE) {
        oflag = _O_WRONLY;
    }
    if (flag & FNBLOCK) {
        DEBUGF("FNBLOCK ignored for %s", devname);
    }
    if (flag & FRLOCK) {
        DEBUGF("FRLOCK ignored for %s", devname);
    }
    if (flag & FWLOCK) {
        DEBUGF("FWLOCK ignored for %s", devname);
    }
    if (flag & FAPPEND) {
        oflag |= _O_APPEND;
    }
    if (flag & FCREAT) {
        oflag |= _O_CREAT;
    }
    if (flag & FTRUNC) {
        oflag |= _O_TRUNC;
    }
    if (flag & FSCAN) {
        DEBUGF("FSCAN ignored for %s", devname);
    }
    if (flag & FRCOM) {
        DEBUGF("FRCOM ignored for %s", devname);
    }
    if (flag & FNBUF) {
        DEBUGF("FNBUF ignored for %s", devname);
    }
    if (flag & FASYNC) {
        DEBUGF("FASYNC ignored for %s", devname);
    }
    if (flag & 0x10000) {
        DEBUGF("0x10000 ignored for %s", devname);
    }

    char path[4096];
    Psyz_AdjustPath(path, devname, sizeof(path));
    if (!(oflag & _O_CREAT)) {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
            WARNF("path '%s' mapped from '%s' is not a file", path, devname);
            return -1;
        }
    }
    wchar_t* wide = file_path_utf16(path);
    if (!wide)
        return -1;
    int fd = -1;
    errno_t err = _wsopen_s(&fd, wide, oflag | _O_BINARY, _SH_DENYNO,
                            oflag & _O_CREAT ? _S_IREAD | _S_IWRITE : 0);
    SDL_free(wide);
    return err ? -1 : fd;
}

int psyz_close(int fd) { return _close(fd); }

// The BIOS offset is signed: SEEK_CUR / SEEK_END may move backwards.
unsigned long psyz_lseek(int fd, unsigned int offset, int flag) {
    return (unsigned long)_lseek(fd, (long)(int)offset, flag);
}

int psyz_read(int fd, void* buf, unsigned int n) { return _read(fd, buf, n); }

int psyz_write(int fd, const void* buf, unsigned int n) {
    return _write(fd, buf, n);
}

long psyz_ioctl(int fd, int com, int arg) {
    NOT_IMPLEMENTED;
    return -1;
}
