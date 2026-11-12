#ifndef PSYZ_FILE_H
#define PSYZ_FILE_H

/**
 * @file file.h
 * @brief Host files named by UTF-8 paths.
 *
 * Paths handed to PsyZ (disc images, memory card directories) and those SDL
 * reports (dialogs, preference directories) are UTF-8. The C runtime's narrow
 * `fopen` on Windows takes the ANSI code page instead, so non-ASCII paths
 * open the wrong file or none; these wrappers use the wide-character APIs
 * there (and lift the MAX_PATH limit) and plain stdio elsewhere. Offsets are
 * 64-bit on every platform (`long` is 32 bits on Windows).
 */

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open a host file like `fopen`
 *
 * @param path UTF-8 path, absolute or relative to the working directory;
 *             either separator works on Windows
 * @param mode `fopen` mode string
 * @return the stream, or NULL with errno set
 */
FILE* Psyz_FileOpen(const char* path, const char* mode);

/**
 * @brief `fseek` with a 64-bit offset
 * @return 0 on success
 */
int Psyz_FileSeek(FILE* file, int64_t offset, int whence);

/**
 * @brief `ftell` with a 64-bit result
 * @return the position, or -1 on error
 */
int64_t Psyz_FileTell(FILE* file);

#ifdef __cplusplus
}
#endif

#endif
