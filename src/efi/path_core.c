#include "efi/path_core.h"

/* Decode one UTF-8 character; returns bytes consumed or -1. BMP only. */
static int utf8_next(const unsigned char *s, uint16_t *cp) {
    if (s[0] < 0x80) {
        *cp = s[0];
        return 1;
    }
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        *cp = (uint16_t)((s[0] & 0x1f) << 6 | (s[1] & 0x3f));
        return *cp >= 0x80 ? 2 : -1; /* reject overlong */
    }
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        *cp = (uint16_t)((s[0] & 0x0f) << 12 | (s[1] & 0x3f) << 6 | (s[2] & 0x3f));
        if (*cp < 0x800 || (*cp >= 0xd800 && *cp <= 0xdfff)) {
            return -1;
        }
        return 3;
    }
    return -1; /* 4-byte (non-BMP) or malformed */
}

static int is_sep(uint16_t c) {
    return c == '/' || c == '\\';
}

/* Append one component to out (which holds an absolute path, len units). */
static int push_component(uint16_t *out, size_t *len, const uint16_t *comp, size_t n) {
    if (n == 1 && comp[0] == '.') {
        return 0;
    }
    if (n == 2 && comp[0] == '.' && comp[1] == '.') {
        if (*len <= 1) {
            return -1; /* above the root */
        }
        while (*len > 1 && out[*len - 1] != '\\') {
            (*len)--;
        }
        if (*len > 1) {
            (*len)--; /* drop the separator too, unless it is the root */
        }
        return 0;
    }
    size_t need = (*len > 1 ? 1 : 0) + n;
    if (*len + need > PATH_MAX_UNITS) {
        return -1;
    }
    if (*len > 1) {
        out[(*len)++] = '\\';
    }
    for (size_t i = 0; i < n; i++) {
        out[(*len)++] = comp[i];
    }
    return 0;
}

/* Feed a UCS-2 or UTF-8 sequence of components into out. */
static int push_ucs2_path(uint16_t *out, size_t *len, const uint16_t *p) {
    uint16_t comp[PATH_MAX_UNITS];
    size_t n = 0;
    for (;; p++) {
        if (!*p || is_sep(*p)) {
            if (n && push_component(out, len, comp, n) < 0) {
                return -1;
            }
            n = 0;
            if (!*p) {
                return 0;
            }
        } else {
            if (n >= PATH_MAX_UNITS) {
                return -1;
            }
            comp[n++] = *p;
        }
    }
}

int path_translate(const char *path, const uint16_t *base_dir, uint16_t *out) {
    uint16_t u[PATH_MAX_UNITS + 1];
    size_t n = 0;
    const unsigned char *s = (const unsigned char *)path;
    while (*s) {
        uint16_t cp;
        int k = utf8_next(s, &cp);
        if (k < 0 || n >= PATH_MAX_UNITS) {
            return -1;
        }
        u[n++] = cp;
        s += k;
    }
    u[n] = 0;

    size_t len = 1;
    out[0] = '\\';
    if (n == 0 || !is_sep(u[0])) {
        if (push_ucs2_path(out, &len, base_dir) < 0) {
            return -1;
        }
    }
    if (push_ucs2_path(out, &len, u) < 0) {
        return -1;
    }
    out[len] = 0;
    return 0;
}

int path_dirname(const uint16_t *file_path, uint16_t *out, size_t cap) {
    size_t len = 0, last = 0;
    while (file_path[len]) {
        if (is_sep(file_path[len])) {
            last = len;
        }
        len++;
    }
    size_t keep = last ? last : 1; /* root stays "\" */
    if (keep + 1 > cap) {
        return -1;
    }
    for (size_t i = 0; i < keep; i++) {
        out[i] = is_sep(file_path[i]) ? '\\' : file_path[i];
    }
    out[0] = '\\';
    out[keep] = 0;
    return 0;
}
