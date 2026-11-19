/* Writes the log into volatile DoomLog UEFI variables (devloop profile). */
#ifndef UD_EFI_DLOG_H
#define UD_EFI_DLOG_H

#include <stddef.h>
#include <stdint.h>

#include "efi/ring_core.h"

typedef void (*dlog_err_fn)(const char *s, size_t n);

/**
 * Replace any existing DoomLog variables with the contents of `ring`, then
 * write DoomLogInfo last. Errors are reported through `err` (never through the
 * log itself, which is being read) and otherwise ignored.
 */
void dlog_write(const log_ring *ring, uint16_t exit_reason, uint64_t exit_status,
                uint64_t run_time_us, const char *build_id, dlog_err_fn err);

#endif
