/* POSIX-style UTF-8 paths -> UEFI UCS-2 paths on the image's volume.
 * Pure code: no UEFI dependencies, unit-tested on the host.
 * See docs/architecture.md#files. */
#ifndef UD_EFI_PATH_CORE_H
#define UD_EFI_PATH_CORE_H

#include <stddef.h>
#include <stdint.h>

#define PATH_MAX_UNITS 260

/**
 * Translate `path` (UTF-8, '/' or '\' separators) to an absolute UCS-2 path in
 * `out` (capacity PATH_MAX_UNITS + 1). Relative paths resolve against
 * `base_dir` (UCS-2, absolute, e.g. L"\\EFI\\DOOM"). "." and ".." components
 * are resolved; ".." above the root, non-BMP characters, malformed UTF-8 and
 * over-long results are rejected.
 * Returns 0 on success, -1 on error.
 */
int path_translate(const char *path, const uint16_t *base_dir, uint16_t *out);

/**
 * Directory part of an absolute UCS-2 file path, e.g. L"\\EFI\\DOOM\\X.EFI" ->
 * L"\\EFI\\DOOM" (root stays L"\\"). Returns 0, or -1 if it does not fit.
 */
int path_dirname(const uint16_t *file_path, uint16_t *out, size_t cap);

#endif
