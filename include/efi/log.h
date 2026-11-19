/* Levelled logging with ConOut, RAM-ring, QEMU debugcon and UEFI-variable sinks.
 * See docs/architecture.md#logging. */
#ifndef UD_EFI_LOG_H
#define UD_EFI_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level;

/** Initialise the log; ConOut sink starts enabled. Call first in efi_main. */
void log_init(void);

/** Log a formatted message; a newline is appended if missing. */
void log_msg(log_level level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_vmsg(log_level level, const char *fmt, va_list ap);

/** Log raw bytes (no prefix, no newline added). Used by stdio. */
void log_write(log_level level, const char *s, size_t n);

/**
 * Also keep the log in `path` (relative to the image directory): written at
 * exit and, if `sync_secs` > 0, rewritten that often so it survives a reset.
 * Requires fs_init() and clock_init(). Registers a cleanup entry, so call it
 * after fs_init() to run before the volume is closed.
 */
void log_file_enable(const char *path, uint32_t sync_secs);

/** Enable/disable the ConOut sink (off while graphics are on screen). */
void log_set_conout(bool enabled);

/**
 * Final flush at exit: writes the DoomLog variables (devloop profile).
 * Must run after all cleanups and before the heap is freed.
 */
void log_finish(uint16_t exit_reason, uint64_t exit_status, uint64_t run_time_us);

#endif
