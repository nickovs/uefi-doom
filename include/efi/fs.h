/* Files on the volume the application was loaded from. Paths are UTF-8,
 * POSIX-style, relative to the image directory unless they start with a separator.
 * See docs/architecture.md#files. */
#ifndef UD_EFI_FS_H
#define UD_EFI_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct fs_file fs_file;

typedef enum {
    FS_READ,   /* read only */
    FS_WRITE,  /* create or truncate */
    FS_UPDATE, /* read+write, must exist */
} fs_mode;

/** Open the image's volume and record its directory. Returns false if unavailable. */
bool fs_init(void);

/** True if the volume refused a write as read-only (logged once). */
bool fs_read_only(void);

/** Open `path`; returns NULL and sets *err to a negative errno on failure. */
fs_file *fs_open(const char *path, fs_mode mode, int *err);

/** Read up to n bytes; returns bytes read (0 at EOF) or a negative errno. */
long fs_read(fs_file *f, void *buf, size_t n);

/** Write n bytes; returns n or a negative errno. */
long fs_write(fs_file *f, const void *buf, size_t n);

int fs_seek(fs_file *f, uint64_t pos);
uint64_t fs_tell(fs_file *f);
/** File size in bytes, or a negative errno. */
int64_t fs_size(fs_file *f);
int fs_flush(fs_file *f);
/** Close (flushes); returns 0 or a negative errno. */
int fs_close(fs_file *f);

int fs_delete(const char *path);
int fs_mkdir(const char *path);
/** Rename within one directory (UEFI SetInfo); -EINVAL across directories. */
int fs_rename(const char *from, const char *to);

/**
 * Read a whole file into page memory (freed automatically at exit).
 * Returns 0 and sets *buf and *len, or a negative errno.
 */
int fs_read_whole(const char *path, void **buf, size_t *len);

/** Write `len` bytes to `path`, replacing it. Returns 0 or a negative errno. */
int fs_write_whole(const char *path, const void *buf, size_t len);

#endif
