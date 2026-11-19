/* Command-line handling: UCS-2 -> UTF-8 conversion and tokenising of boot-entry
 * LoadOptions. Pure code: no UEFI dependencies, unit-tested on the host. */
#ifndef UD_EFI_ARGV_CORE_H
#define UD_EFI_ARGV_CORE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Return 1 if `size_bytes` of LoadOptions at `opts` look like UCS-2 text:
 * even, non-zero size and only printable BMP characters (or tab) before the
 * first NUL. Binary optional data from boot entries returns 0.
 */
int argv_is_text(const uint16_t *opts, size_t size_bytes);

/**
 * Convert NUL-terminated UCS-2 `s` (at most `max_units` units) to UTF-8 in
 * `out` (capacity `cap`, always NUL-terminated). Returns bytes written
 * excluding the NUL, or -1 if it did not fit.
 */
int argv_ucs2_to_utf8(const uint16_t *s, size_t max_units, char *out, size_t cap);

/**
 * Tokenise text LoadOptions into argv. `argv[0]` is set to `progname`; tokens
 * follow, split on whitespace with double-quote grouping. Token storage is
 * carved from `buf`. Returns argc, or -1 if `buf` or `max_args` is too small.
 */
int argv_from_load_options(const uint16_t *opts, size_t size_bytes, const char *progname, char *buf,
                           size_t bufsize, char **argv, int max_args);

#endif
