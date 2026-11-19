/* In-memory stand-in for efi/fs.h and the log sink, for host tests of the
 * FILE shim. Paths are compared verbatim; at most 8 files. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "efi/fs.h"
#include "efi/log.h"
#include "fake_fs.h"

static struct fake_file {
    char name[64];
    unsigned char *data;
    size_t len;
    bool used;
} files[8];

struct fs_file {
    struct fake_file *f;
    uint64_t pos;
    bool writable;
};

char fake_log[4096];
size_t fake_log_len;
int fake_fs_reads, fake_fs_writes;

static struct fake_file *find(const char *path) {
    for (int i = 0; i < 8; i++) {
        if (files[i].used && strcmp(files[i].name, path) == 0) {
            return &files[i];
        }
    }
    return NULL;
}

void fake_fs_put(const char *path, const void *data, size_t len) {
    struct fake_file *f = find(path);
    for (int i = 0; !f && i < 8; i++) {
        if (!files[i].used) {
            f = &files[i];
            f->used = true;
            strncpy(f->name, path, sizeof f->name - 1);
        }
    }
    free(f->data);
    f->data = malloc(len ? len : 1);
    memcpy(f->data, data, len);
    f->len = len;
}

const void *fake_fs_get(const char *path, size_t *len) {
    struct fake_file *f = find(path);
    if (!f) {
        return NULL;
    }
    *len = f->len;
    return f->data;
}

fs_file *fs_open(const char *path, fs_mode mode, int *err) {
    struct fake_file *f = find(path);
    if (!f && mode != FS_WRITE) {
        *err = -ENOENT;
        return NULL;
    }
    if (!f || mode == FS_WRITE) {
        fake_fs_put(path, "", 0);
        f = find(path);
    }
    fs_file *h = calloc(1, sizeof *h);
    h->f = f;
    h->writable = mode != FS_READ;
    *err = 0;
    return h;
}

long fs_read(fs_file *h, void *buf, size_t n) {
    fake_fs_reads++;
    size_t avail = h->pos < h->f->len ? h->f->len - (size_t)h->pos : 0;
    size_t k = n < avail ? n : avail;
    memcpy(buf, h->f->data + h->pos, k);
    h->pos += k;
    return (long)k;
}

long fs_write(fs_file *h, const void *buf, size_t n) {
    fake_fs_writes++;
    if (!h->writable) {
        return -EACCES;
    }
    size_t end = (size_t)h->pos + n;
    if (end > h->f->len) {
        h->f->data = realloc(h->f->data, end);
        memset(h->f->data + h->f->len, 0, end - h->f->len);
        h->f->len = end;
    }
    memcpy(h->f->data + h->pos, buf, n);
    h->pos = end;
    return (long)n;
}

int fs_seek(fs_file *h, uint64_t pos) {
    h->pos = pos;
    return 0;
}

uint64_t fs_tell(fs_file *h) {
    return h->pos;
}

int64_t fs_size(fs_file *h) {
    return (int64_t)h->f->len;
}

int fs_flush(fs_file *h) {
    (void)h;
    return 0;
}

int fs_close(fs_file *h) {
    free(h);
    return 0;
}

int fs_delete(const char *path) {
    struct fake_file *f = find(path);
    if (!f) {
        return -ENOENT;
    }
    free(f->data);
    memset(f, 0, sizeof *f);
    return 0;
}

int fs_rename(const char *from, const char *to) {
    struct fake_file *f = find(from);
    if (!f) {
        return -ENOENT;
    }
    if (find(to)) {
        return -EACCES; /* like UEFI SetInfo onto an existing name */
    }
    strncpy(f->name, to, sizeof f->name - 1);
    return 0;
}

void log_write(log_level level, const char *s, size_t n) {
    (void)level;
    if (fake_log_len + n < sizeof fake_log) {
        memcpy(fake_log + fake_log_len, s, n);
        fake_log_len += n;
        fake_log[fake_log_len] = 0;
    }
}
