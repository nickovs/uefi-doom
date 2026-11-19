/* WAD header and directory validation (pure, host-tested). The engine trusts
 * lump offsets and sizes, so files are checked before it sees them.
 * See docs/architecture.md#files. */
#ifndef UD_PORT_WAD_CHECK_H
#define UD_PORT_WAD_CHECK_H

#include <stddef.h>
#include <stdint.h>

/**
 * Check that `buf[0..len)` is an IWAD/PWAD whose directory and every lump lie
 * inside the buffer. Returns 0, or -1 with a reason in `why`.
 */
int wad_check(const uint8_t *buf, size_t len, char *why, size_t why_len);

#endif
