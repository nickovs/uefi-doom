/* WAD file class that reads the whole file into page memory and hands the
 * engine a mapped pointer (zero-copy lump access). Validates .wad files first.
 * The pages stay until exit (freed by the allocator's cleanup). */
#include <stdlib.h>
#include <string.h>

#include "efi/fs.h"
#include "efi/log.h"
#include "port/wad_check.h"

#include "i_system.h"
#include "w_file.h"

extern wad_file_class_t mem_wad_file;

static wad_file_t *mem_open(char *path) {
    void *buf;
    size_t len;
    int r = fs_read_whole(path, &buf, &len);
    if (r < 0) {
        log_msg(LOG_DEBUG, "wad: %s: cannot read (errno %d)", path, -r);
        return NULL;
    }
    size_t n = strlen(path);
    if (n >= 4 && strcasecmp(path + n - 4, ".wad") == 0) {
        char why[96];
        if (wad_check(buf, len, why, sizeof why) < 0) {
            I_Error("%s: %s", path, why);
        }
    } else if (len > 0xffffffffu) {
        I_Error("%s: file larger than 4 GiB", path);
    }
    wad_file_t *w = malloc(sizeof *w);
    if (!w) {
        I_Error("%s: out of memory", path);
    }
    w->file_class = &mem_wad_file;
    w->mapped = buf;
    w->length = (unsigned int)len;
    log_msg(LOG_INFO, "wad: %s mapped (%zu bytes)", path, len);
    return w;
}

static void mem_close(wad_file_t *w) {
    free(w);
}

static size_t mem_read(wad_file_t *w, unsigned int offset, void *buffer, size_t len) {
    if (offset >= w->length) {
        return 0;
    }
    size_t n = w->length - offset < len ? w->length - offset : len;
    memcpy(buffer, w->mapped + offset, n);
    return n;
}

wad_file_class_t mem_wad_file = {mem_open, mem_close, mem_read};
