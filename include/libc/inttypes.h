/* Minimal <inttypes.h> for the UEFI build (LLP64: long is 32 bits). */
#ifndef UD_LIBC_INTTYPES_H
#define UD_LIBC_INTTYPES_H

#include <stdint.h>

#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"

#endif
