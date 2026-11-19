/* Minimal <unistd.h> for the UEFI build. */
#ifndef UD_LIBC_UNISTD_H
#define UD_LIBC_UNISTD_H

#include <stddef.h>

int isatty(int fd);
int unlink(const char *path);
int usleep(unsigned int us);

#endif
