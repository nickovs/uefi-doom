#include "efi/argv_core.h"

#include <string.h>

static int is_text_unit(uint16_t c) {
    if (c == '\t') {
        return 1;
    }
    if (c < 0x20 || (c >= 0x7f && c < 0xa0)) {
        return 0; /* control characters */
    }
    if (c >= 0xd800 && c <= 0xdfff) {
        return 0; /* surrogates: UCS-2 has none */
    }
    return c != 0xfffe && c != 0xffff;
}

int argv_is_text(const uint16_t *opts, size_t size_bytes) {
    if (!opts || size_bytes == 0 || size_bytes % 2) {
        return 0;
    }
    size_t units = size_bytes / 2;
    for (size_t i = 0; i < units && opts[i]; i++) {
        if (!is_text_unit(opts[i])) {
            return 0;
        }
    }
    return 1;
}

static int put_utf8(uint16_t c, char *out, size_t cap, size_t *pos) {
    unsigned char enc[3];
    size_t n;
    if (c < 0x80) {
        enc[0] = (unsigned char)c;
        n = 1;
    } else if (c < 0x800) {
        enc[0] = (unsigned char)(0xc0 | (c >> 6));
        enc[1] = (unsigned char)(0x80 | (c & 0x3f));
        n = 2;
    } else {
        enc[0] = (unsigned char)(0xe0 | (c >> 12));
        enc[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3f));
        enc[2] = (unsigned char)(0x80 | (c & 0x3f));
        n = 3;
    }
    if (*pos + n >= cap) {
        return -1; /* keep room for the NUL */
    }
    memcpy(out + *pos, enc, n);
    *pos += n;
    return 0;
}

int argv_ucs2_to_utf8(const uint16_t *s, size_t max_units, char *out, size_t cap) {
    size_t pos = 0;
    if (cap == 0) {
        return -1;
    }
    for (size_t i = 0; i < max_units && s[i]; i++) {
        if (put_utf8(s[i], out, cap, &pos) < 0) {
            out[pos] = 0;
            return -1;
        }
    }
    out[pos] = 0;
    return (int)pos;
}

int argv_from_load_options(const uint16_t *opts, size_t size_bytes, const char *progname, char *buf,
                           size_t bufsize, char **argv, int max_args) {
    size_t pos = 0;
    int argc = 0;
    size_t plen = strlen(progname) + 1;
    if (max_args < 2 || plen > bufsize) {
        return -1;
    }
    memcpy(buf, progname, plen);
    argv[argc++] = buf;
    pos = plen;

    if (argv_is_text(opts, size_bytes)) {
        size_t units = size_bytes / 2;
        size_t i = 0;
        while (i < units && opts[i]) {
            while (i < units && (opts[i] == ' ' || opts[i] == '\t')) {
                i++;
            }
            if (i >= units || !opts[i]) {
                break;
            }
            if (argc >= max_args - 1) {
                return -1; /* leave room for the terminating NULL */
            }
            char *tok = buf + pos;
            int quoted = 0;
            while (i < units && opts[i] && (quoted || (opts[i] != ' ' && opts[i] != '\t'))) {
                if (opts[i] == '"') {
                    quoted = !quoted;
                } else if (put_utf8(opts[i], buf, bufsize, &pos) < 0) {
                    return -1;
                }
                i++;
            }
            if (pos >= bufsize) {
                return -1;
            }
            buf[pos++] = 0;
            argv[argc++] = tok;
        }
    }
    argv[argc] = NULL;
    return argc;
}
