/* Minimal <sys/types.h> for the UEFI build. */
#ifndef UD_LIBC_SYS_TYPES_H
#define UD_LIBC_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int64_t ssize_t;
typedef int64_t off_t;
typedef unsigned int mode_t;

#endif
