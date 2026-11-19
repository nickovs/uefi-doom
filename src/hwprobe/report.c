/* Minimal streaming JSON builder for HWPROBE.JSON, plus shared helpers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efi/fs.h"
#include "efi/log.h"
#include "hwprobe.h"
#include "input/input_hw.h"

#define MAX_DEPTH 16

static char *buf;
static size_t len, cap;
static bool oom;
static int depth;                  /* open containers */
static bool need_comma[MAX_DEPTH]; /* per level: an item was already written */
static char closer[MAX_DEPTH];     /* '}' or ']' per level */

static void put(const char *s, size_t n) {
    if (oom) {
        return;
    }
    if (len + n + 1 > cap) {
        size_t nc = cap ? cap * 2 : 4096;
        while (nc < len + n + 1) {
            nc *= 2;
        }
        char *nb = realloc(buf, nc);
        if (!nb) {
            oom = true;
            return;
        }
        buf = nb;
        cap = nc;
    }
    memcpy(buf + len, s, n);
    len += n;
    buf[len] = 0;
}

static void put_s(const char *s) {
    put(s, strlen(s));
}

static void put_string(const char *s) {
    put("\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            char esc[2] = {'\\', (char)c};
            put(esc, 2);
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\u%04x", c);
            put_s(esc);
        } else {
            put((const char *)&c, 1);
        }
    }
    put("\"", 1);
}

/* Separator and (inside objects) the key for the next item. */
static void item(const char *k) {
    if (depth > 0) {
        if (need_comma[depth - 1]) {
            put(",", 1);
        }
        need_comma[depth - 1] = true;
    }
    if (k) {
        put_string(k);
        put(":", 1);
    }
}

static void open(const char *k, char open_c, char close_c) {
    item(k);
    put(&open_c, 1);
    if (depth < MAX_DEPTH) {
        closer[depth] = close_c;
        need_comma[depth++] = false;
    }
}

void rep_obj(const char *k) {
    open(k, '{', '}');
}

void rep_arr(const char *k) {
    open(k, '[', ']');
}

void rep_end(void) {
    if (depth > 0) {
        depth--;
        put(&closer[depth], 1);
    }
}

void rep_str(const char *k, const char *v) {
    item(k);
    put_string(v ? v : "");
}

void rep_int(const char *k, int64_t v) {
    char s[32];
    snprintf(s, sizeof s, "%lld", (long long)v);
    item(k);
    put_s(s);
}

void rep_hex(const char *k, uint64_t v) {
    char s[24];
    snprintf(s, sizeof s, "0x%llx", (unsigned long long)v);
    rep_str(k, s);
}

void rep_bool(const char *k, bool v) {
    item(k);
    put_s(v ? "true" : "false");
}

void rep_num(const char *k, double v) {
    char s[48];
    snprintf(s, sizeof s, "%.6g", v);
    item(k);
    put_s(s);
}

int rep_save(const char *path) {
    while (depth > 0) {
        rep_end();
    }
    if (oom || !buf) {
        return -12; /* ENOMEM */
    }
    put("\n", 1);
    int r = fs_write_whole(path, buf, len);
    free(buf);
    buf = NULL;
    len = cap = 0;
    return r;
}

bool hw_ask(const char *question, uint32_t timeout_ms) {
    if (hw_batch) {
        return false;
    }
    uint16_t scan, ch;
    uint32_t sh;
    while (conin_read(&scan, &ch, &sh)) {
        /* discard type-ahead */
    }
    log_msg(LOG_INFO, "%s  Press Y for yes, N for no (%u s).", question, timeout_ms / 1000);
    if (!conin_wait_key(timeout_ms) || !conin_read(&scan, &ch, &sh)) {
        log_msg(LOG_INFO, "  -> no answer");
        return false;
    }
    bool yes = ch == 'y' || ch == 'Y';
    log_msg(LOG_INFO, "  -> %s", yes ? "yes" : "no");
    return yes;
}

bool hw_wait_enter(const char *next, uint32_t timeout_ms) {
    if (hw_batch) {
        return false;
    }
    uint16_t scan, ch;
    uint32_t sh;
    while (conin_read(&scan, &ch, &sh)) {
    }
    log_msg(LOG_INFO, "");
    log_msg(LOG_INFO, ">>> NEXT: %s", next);
    log_msg(LOG_INFO, ">>> Press ENTER when ready (%u s).", timeout_ms / 1000);
    while (conin_wait_key(timeout_ms)) {
        if (conin_read(&scan, &ch, &sh) && (ch == '\r' || ch == '\n')) {
            return true;
        }
    }
    log_msg(LOG_INFO, "    (no ENTER: skipped)");
    return false;
}

uint64_t hw_median(uint64_t *v, int n) {
    for (int i = 1; i < n; i++) {
        uint64_t x = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    return n ? v[n / 2] : 0;
}
