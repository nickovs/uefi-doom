/* Minimal <sys/stat.h> for the UEFI build. */
#ifndef UD_LIBC_SYS_STAT_H
#define UD_LIBC_SYS_STAT_H

#include <sys/types.h>

/** Create a directory relative to the image directory; `mode` is ignored. */
int mkdir(const char *path, mode_t mode);

#endif
