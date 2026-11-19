/* FILE over efi/fs: one 4 KiB buffer used for either reading or writing.
 * stdout/stderr are special FILEs that write to the log (INFO / WARN).
 * Pure apart from the fs/log calls, so tests/unit links it against a fake fs. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efi/fs.h"
#include "efi/log.h"
#include "libc_pure.h"

#define FILE_BUF 4096

enum buf_state { BUF_IDLE, BUF_READ, BUF_WRITE };

struct ud_file {
    fs_file *fs; /* NULL for the log streams */
    log_level level;
    bool can_read, can_write, eof, err;
    enum buf_state state;
    size_t pos, len; /* BUF_READ: next byte and bytes held; BUF_WRITE: len pending */
    unsigned char buf[FILE_BUF];
};

static struct ud_file out_file = {.level = LOG_INFO, .can_write = true};
static struct ud_file err_file = {.level = LOG_WARN, .can_write = true};
struct ud_file *LIBC(stdout) = &out_file;
struct ud_file *LIBC(stderr) = &err_file;

static int fail(struct ud_file *f, int neg_errno) {
    if (f) {
        f->err = true;
    }
    errno = -neg_errno;
    return -1;
}

struct ud_file *LIBC(fopen)(const char *path, const char *mode) {
    fs_mode m;
    bool plus = strchr(mode, '+') != NULL;
    if (mode[0] == 'r') {
        m = plus ? FS_UPDATE : FS_READ;
    } else if (mode[0] == 'w' && !plus) {
        m = FS_WRITE;
    } else {
        errno = EINVAL; /* append and w+ are not used by the engine */
        return NULL;
    }
    struct ud_file *f = calloc(1, sizeof *f);
    if (!f) {
        errno = ENOMEM;
        return NULL;
    }
    int err;
    f->fs = fs_open(path, m, &err);
    if (!f->fs) {
        free(f);
        errno = -err;
        return NULL;
    }
    f->can_read = m != FS_WRITE;
    f->can_write = m != FS_READ;
    return f;
}

/* Push pending writes to fs, or drop read-ahead (repositioning fs to the
 * logical position). Leaves the buffer idle. */
static int settle(struct ud_file *f) {
    if (f->state == BUF_WRITE && f->len) {
        long r = fs_write(f->fs, f->buf, f->len);
        if (r < 0) {
            f->state = BUF_IDLE;
            f->len = 0;
            return fail(f, (int)r);
        }
    } else if (f->state == BUF_READ && f->pos < f->len) {
        int r = fs_seek(f->fs, fs_tell(f->fs) - (f->len - f->pos));
        if (r < 0) {
            return fail(f, r);
        }
    }
    f->state = BUF_IDLE;
    f->pos = f->len = 0;
    return 0;
}

int LIBC(fflush)(struct ud_file *f) {
    if (!f || !f->fs) {
        return 0;
    }
    if (settle(f) < 0) {
        return EOF;
    }
    int r = fs_flush(f->fs);
    return r < 0 ? fail(f, r) : 0;
}

int LIBC(fclose)(struct ud_file *f) {
    if (!f || !f->fs) {
        return 0;
    }
    int r = settle(f);
    int c = fs_close(f->fs);
    free(f);
    if (c < 0) {
        errno = -c;
    }
    return r < 0 || c < 0 ? EOF : 0;
}

size_t LIBC(fread)(void *buf, size_t size, size_t n, struct ud_file *f) {
    size_t want = size * n, got = 0;
    if (!f->fs || !f->can_read || !want) {
        return 0;
    }
    if (f->state == BUF_WRITE && settle(f) < 0) {
        return 0;
    }
    unsigned char *d = buf;
    while (got < want) {
        if (f->state == BUF_READ && f->pos < f->len) {
            size_t k = f->len - f->pos < want - got ? f->len - f->pos : want - got;
            memcpy(d + got, f->buf + f->pos, k);
            f->pos += k;
            got += k;
            continue;
        }
        f->state = BUF_IDLE;
        f->pos = f->len = 0;
        bool direct = want - got >= FILE_BUF;
        long r = fs_read(f->fs, direct ? d + got : f->buf, direct ? want - got : FILE_BUF);
        if (r < 0) {
            fail(f, (int)r);
            break;
        }
        if (r == 0) {
            f->eof = true;
            break;
        }
        if (direct) {
            got += (size_t)r;
        } else {
            f->state = BUF_READ;
            f->len = (size_t)r;
        }
    }
    return got / size;
}

size_t LIBC(fwrite)(const void *buf, size_t size, size_t n, struct ud_file *f) {
    size_t want = size * n;
    if (!want) {
        return 0;
    }
    if (!f->fs) {
        log_write(f->level, buf, want);
        return n;
    }
    if (!f->can_write) {
        fail(f, -EBADF);
        return 0;
    }
    if (f->state == BUF_READ && settle(f) < 0) {
        return 0;
    }
    const unsigned char *s = buf;
    size_t done = 0;
    while (done < want) {
        f->state = BUF_WRITE;
        size_t k = FILE_BUF - f->len < want - done ? FILE_BUF - f->len : want - done;
        memcpy(f->buf + f->len, s + done, k);
        f->len += k;
        done += k;
        if (f->len == FILE_BUF && settle(f) < 0) {
            return 0; /* buffered data lost with the error, as with a real disk */
        }
    }
    return n;
}

long LIBC(ftell)(struct ud_file *f) {
    if (!f->fs) {
        return fail(f, -EINVAL);
    }
    uint64_t p = fs_tell(f->fs);
    if (f->state == BUF_READ) {
        p -= f->len - f->pos;
    } else if (f->state == BUF_WRITE) {
        p += f->len;
    }
    return (long)p;
}

int LIBC(fseek)(struct ud_file *f, long off, int whence) {
    if (!f->fs) {
        return fail(f, -EINVAL);
    }
    long base = 0;
    if (whence == SEEK_CUR) {
        base = LIBC(ftell)(f);
    } else if (whence == SEEK_END) {
        if (settle(f) < 0) {
            return -1;
        }
        int64_t sz = fs_size(f->fs);
        if (sz < 0) {
            return fail(f, (int)sz);
        }
        base = (long)sz;
    } else if (whence != SEEK_SET) {
        return fail(NULL, -EINVAL);
    }
    if (base + off < 0) {
        return fail(NULL, -EINVAL);
    }
    if (settle(f) < 0) {
        return -1;
    }
    int r = fs_seek(f->fs, (uint64_t)(base + off));
    if (r < 0) {
        return fail(f, r);
    }
    f->eof = false;
    return 0;
}

int LIBC(feof)(struct ud_file *f) {
    return f->eof;
}

int LIBC(ferror)(struct ud_file *f) {
    return f->err;
}

int LIBC(fgetc)(struct ud_file *f) {
    unsigned char c;
    return LIBC(fread)(&c, 1, 1, f) == 1 ? c : EOF;
}

int LIBC(fputc)(int c, struct ud_file *f) {
    unsigned char ch = (unsigned char)c;
    return LIBC(fwrite)(&ch, 1, 1, f) == 1 ? ch : EOF;
}

char *LIBC(fgets)(char *buf, int n, struct ud_file *f) {
    int i = 0;
    while (i < n - 1) {
        int c = LIBC(fgetc)(f);
        if (c == EOF) {
            break;
        }
        buf[i++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    if (i == 0 || n < 1) {
        return NULL;
    }
    buf[i] = 0;
    return buf;
}

/* shortcut: scans one line at a time (lines up to 1023 bytes), enough for the
 * engine's line-per-setting config; a conversion cannot span lines. */
int LIBC(fscanf)(struct ud_file *f, const char *fmt, ...) {
    char line[1024];
    if (!LIBC(fgets)(line, sizeof line, f)) {
        return EOF;
    }
    va_list ap;
    va_start(ap, fmt);
    int r = LIBC(vsscanf)(line, fmt, ap);
    va_end(ap);
    return r;
}

int LIBC(fputs)(const char *s, struct ud_file *f) {
    size_t n = strlen(s);
    return LIBC(fwrite)(s, 1, n, f) == n || !n ? 0 : EOF;
}

int LIBC(vfprintf)(struct ud_file *f, const char *fmt, va_list ap) {
    char small[512];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(small, sizeof small, fmt, ap);
    char *p = small;
    if (n >= (int)sizeof small) {
        p = malloc((size_t)n + 1);
        if (!p) {
            va_end(ap2);
            return fail(f, -ENOMEM);
        }
        vsnprintf(p, (size_t)n + 1, fmt, ap2);
    }
    va_end(ap2);
    if (n > 0 && LIBC(fwrite)(p, 1, (size_t)n, f) != (size_t)n) {
        n = -1;
    }
    if (p != small) {
        free(p);
    }
    return n;
}

int LIBC(fprintf)(struct ud_file *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = LIBC(vfprintf)(f, fmt, ap);
    va_end(ap);
    return r;
}

int LIBC(fileno)(struct ud_file *f) {
    return f == &out_file ? 1 : f == &err_file ? 2 : -1;
}

int LIBC(remove)(const char *path) {
    int r = fs_delete(path);
    return r < 0 ? fail(NULL, r) : 0;
}

int LIBC(rename)(const char *from, const char *to) {
    int r = fs_rename(from, to);
    return r < 0 ? fail(NULL, r) : 0;
}
