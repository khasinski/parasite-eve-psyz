#ifndef PSYZ_GPU_H
#define PSYZ_GPU_H

/**
 * @file gpu.h
 * @brief PS1 GPU command port (GP0/GP1) endpoints.
 */

#include <psyz/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handler for application-defined GP0 commands.
 *
 * Handlers are only called for GP0 opcodes that PsyZ does not implement.
 * Built-in PlayStation GPU commands always take precedence. The handler may
 * inspect multiple queued words and returns the number of words consumed.
 * Returning zero or a negative value is treated as an invalid command.
 */
typedef int (*PsyzGpuCommandHandler)(
    const u_long* words, int available, void* userdata);

/**
 * @brief Register an application-defined GP0 command handler.
 *
 * This lets ports serialize custom renderer state changes in the same FIFO as
 * normal PSY-Q ordering-table commands. Registering NULL removes the handler.
 *
 * @param opcode GP0 command byte (the high 8 bits of the first command word)
 * @param handler Handler to invoke for an otherwise unsupported opcode
 * @param userdata Opaque pointer forwarded to the handler
 * @return 0 on success, negative on invalid input
 */
int Psyz_GpuRegisterCommandHandler(
    unsigned int opcode, PsyzGpuCommandHandler handler, void* userdata);

/**
 * @brief Start recording a GPU dump (ps1dev GPUDUMP format, ".psxgpu").
 *
 * The dump begins with the current display mode, the whole VRAM and the
 * current drawing state, then records every GP0 command and GP1 display
 * command until Psyz_GpuDumpEnd. DuckStation replays such a file directly,
 * so a frame drawn by PsyZ can be compared with a reference PS1 GPU.
 *
 * @return 0 on success, negative when the file cannot be written
 */
int Psyz_GpuDumpBegin(const char* path);

/**
 * @brief Finish the dump started by Psyz_GpuDumpBegin with a VSync event.
 *
 * When vram_path is not NULL, the VRAM after the recorded commands is also
 * written there as 1024x512 little-endian 16-bit pixels (the layout of
 * DuckStation's VRAM dumps).
 *
 * @return 0 on success, negative when no dump is active or a write failed
 */
int Psyz_GpuDumpEnd(const char* vram_path);

/**
 * @brief Set the horizontal coordinate grid for subsequently rasterized
 * primitives.
 *
 * Primitive X coordinates authored in source_width units are mapped into
 * target_width units. Y coordinates, texture coordinates, VRAM addresses,
 * drawing areas and display configuration are unchanged. Setting equal widths
 * restores normal 1:1 PlayStation coordinates.
 *
 * Changes made directly through this function take effect immediately. When a
 * state change must be ordered relative to an ordering table, call it from an
 * application-defined GP0 command handler.
 *
 * @return 0 on success, negative when either width is zero
 */
int Psyz_GpuSetHorizontalGrid(
    unsigned int source_width, unsigned int target_width);

/** Optional display-only 4x sprite atlas. PNG/BMP contain both planes;
 * WebP contains the material, with a same-size lossless <key>.ref.png plane.
 * Atlas layout: 68px cells (64px image,
 * 2px gutters), at most 16 columns; lower half holds original RGB references.
 * Native VRAM/readback, transparency and STP always use the original texture.
 * Returns 1 when loaded, 0 when unavailable, -1 for malformed/over-budget data.
 * Keys identify immutable assets, not VRAM addresses. */
int Psyz_GpuLoadSpriteAtlas(uint64_t key, const char *filename);
/* Retire queued reads and reuse the bounded atlas storage for a new room. */
void Psyz_GpuResetSpriteAtlases(void);
/* Presentation switch; native VRAM remains unchanged. */
/* Optional display-only midpoint geometry; simulation and native VRAM stay original. */
int Psyz_GpuSetFrameInterpolation(int enabled);
int Psyz_GpuGetFrameInterpolation(void);
int Psyz_GpuGetFrameInterpolationRequested(void);
float Psyz_GpuGetPresentedFps(void);
void Psyz_GpuEnsureInterpolationBackgroundPair(const void *a,const void *b);
unsigned long long Psyz_GpuGetInterpolatedPresentCount(void);
void Psyz_GpuRegisterInterpolationPair(const void *a,const void *b);
void Psyz_GpuRegisterInterpolationBackgroundPair(const void *a,const void *b);
void Psyz_GpuSetInterpolationCamera(int x,int y);
void Psyz_GpuEnsureInterpolationPair(const void *a,const void *b);
void Psyz_GpuCopyInterpolationPacket(const void *from,const void *to);
void Psyz_GpuResetInterpolation(void);
void Psyz_GpuClearInterpolationPackets(void);
void Psyz_GpuBeginInterpolationFrame(void);
void Psyz_GpuPrepareInterpolatedPresent(int next_y);
/* For a renderer whose packets are not at fixed addresses: tags a packet of
 * the frame being built with a stable identity (e.g. model, instance and
 * face), which pairs it with the packet of the same identity in the previous
 * frame. Call between the frame's Psyz_GpuBeginInterpolationFrame calls,
 * before the frame's own; untagged packets (HUD) keep their pose. */
void Psyz_GpuTagInterpolationPacket(const void *packet,unsigned long long id);
/* Like Psyz_GpuPrepareInterpolatedPresent, for a game that waits on VSync(1)
 * over several fields with one VSync(0) per frame: the midpoint is presented
 * at the next blank that passes during that wait (paced presents), and not
 * at all when the next present is a new frame. */
void Psyz_GpuPrepareInterpolatedBlank(int next_y);
void Psyz_GpuSetSpriteAtlasesEnabled(int enabled);
int Psyz_GpuGetSpriteAtlasesEnabled(void);
/* Selects one cell for the next GP0 primitive only. Use a queued user command
 * when selecting inside an OT. Missing keys select ordinary rendering. */
/* Cell bit 23 marks display-only widescreen extension material. */
void Psyz_GpuSelectSpriteAtlas(uint64_t key, unsigned int cell);

/**
 * @brief Keep a CPU copy of the VRAM (off by default)
 *
 * When on, image uploads (LoadImage), fills (ClearImage) and copies
 * (MoveImage) are also applied to a 1024x512 RGB5551 copy in memory, so the
 * application can read texels without downloading from the GPU. Primitives
 * are not rasterized into it: drawing marks the current drawing area unknown
 * instead. Only writes made while it is on are in the copy.
 *
 * @param enabled non-zero to allocate and keep the copy, zero to free it
 * @return 0 on success, negative when out of memory
 */
int Psyz_GpuSetVramCopy(int enabled);

/**
 * @brief Read the CPU copy of the VRAM (see Psyz_GpuSetVramCopy)
 *
 * @param known If not NULL, receives a 1024x512 byte map: non-zero where the
 * copy holds what the VRAM holds, zero where it was never written while the
 * copy was on, or was drawn over since
 * @return 1024x512 RGB5551 pixels, row by row, or NULL when the copy is off
 */
const unsigned short* Psyz_GpuGetVramCopy(const unsigned char** known);

/**
 * @brief Write a GP0 word to the internal GPU queue
 *
 * GP0 (1F801810h) is the PS1 rendering port responsible for drawing primitives
 * and configuring GPU state. On real hardware GP0 words are consumed
 * asynchronously by the GPU FIFO. Here they are buffered in an internal queue
 * and may only flushed when Psyz_GpuExeque is called depending on backend.
 * https://problemkaputt.de/psxspx-graphics-processing-unit-gpu.htm
 *
 * @param word GP0 command word
 */
void Psyz_GpuWriteGP0(unsigned int word);

/**
 * @brief Flush the internal GP0 queue and render to screen
 *
 * Processes all GP0 words accumulated via Psyz_GpuWriteGP0 and submits them
 * to the GPU backend. Depending on the backend, this call may block until all
 * queued packed are consumed, or return immediately. When synchronization is
 * desired, poll until the queue is fully drained:
 * `while ((ret = Psyz_GpuExeque()) > 0);`
 *
 * @return 0: all commands rendered.
 *         Positive: number of commands still pending.
 *         Negative: operation failed.
 */
int Psyz_GpuExeque(void);

/**
 * @brief Write a GP1 word to control the display
 *
 * GP1 (1F801814h) is the PS1 display control port responsible for configuring
 * the display output. GP1 words sent via this endpoint take effect immediately.
 * Depending on the backend, certain commands may be silently ignored if the
 * target does not support the requested property, such as specific resolutions
 * or interlaced modes.
 * https://problemkaputt.de/psxspx-gpu-display-control-commands-gp1.htm
 *
 * @param word GP1 command word
 */
void Psyz_GpuDisplayCommand(unsigned int word);

/* Temporary bounded native resolution-transition metadata observation. */
void Psyz_GpuTraceResolutionRoom(int buffer_y, unsigned tiles, int left, int right, int hd_available, int hd_left, int hd_right, unsigned long long key, unsigned group, unsigned cells, int sidecar_status);

/* Optional dedicated dialogue alpha sheet,416x520 RGBA; no native VRAM edits. */
int Psyz_GpuLoadDialogueFont(const unsigned char *rgba,unsigned bytes);
int Psyz_GpuHasDialogueFont(void);
int Psyz_GpuDialogueStateIdle(void);
void Psyz_GpuSetDialogueFontEnabled(int enabled);
int Psyz_GpuReadDialogueFont(unsigned tpage,unsigned clut,unsigned short *texels,unsigned short *palette);
/* No downloads in generation query; writes touching font/CLUT invalidate. */
uint64_t Psyz_GpuWatchDialogueFont(unsigned page,unsigned clut);
uint64_t Psyz_GpuDialogueFontGeneration(unsigned page,unsigned clut);
void Psyz_GpuInvalidateDialogueFont(void);
void Psyz_GpuSelectDialogueGlyph(unsigned code);
int Psyz_GpuRegisterCommandHandlerIfVacant(unsigned int,PsyzGpuCommandHandler,void *);

/* Native player-owned, exact page/CLUT Duck texture replacements. No mesh change. */
int Psyz_GpuSetHdModels(int enabled, const char *root);
/* Explicit reload stages an index, then retires old reads before commit. */
int Psyz_GpuReloadHdModels(const char *root);
const char *Psyz_GpuHdModelsRoot(void);
int Psyz_GpuHdModelsStateSafe(void);
const char *Psyz_GpuHdModelsStatus(void);
void Psyz_GpuBeginPlayerTextureSet(int bank);
void Psyz_GpuRegisterPlayerTexturePage(unsigned page, unsigned clut);

/* Two independent native font alphabets/palettes. Context0 dialogue,1 menu. */
uint64_t Psyz_GpuWatchFontContext(unsigned context,unsigned page,unsigned clut);
uint64_t Psyz_GpuFontContextGeneration(unsigned context,unsigned page,unsigned clut);
void Psyz_GpuSetFontContextEnabled(unsigned context,int enabled);
int Psyz_GpuHasFontContext(unsigned context);
int Psyz_GpuLoadFontContext(unsigned context,const unsigned char *rgba,unsigned bytes);
void Psyz_GpuSelectFontGlyph(unsigned context,unsigned code,unsigned crop,unsigned width);

#ifdef __cplusplus
}
#endif

#endif

