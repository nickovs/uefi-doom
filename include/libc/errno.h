/* Minimal <errno.h> for the UEFI build. */
#ifndef UD_LIBC_ERRNO_H
#define UD_LIBC_ERRNO_H

extern int errno;

#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENOSPC 28
#define EROFS 30
#define ENAMETOOLONG 36
#define ENOSYS 38

#endif
