/* Number parsing and sscanf (pure C).
 *
 * sscanf supports what the engine uses: whitespace, literals, %d %i %u %x %X
 * %o %s %c %[set] %[^set] %n %%, '*' suppression, field widths and the
 * h/hh/l/ll length modifiers. Anything else stops the scan (the caller sees a short count). */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "libc_pure.h"

static int digit_val(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 99;
}

/* Parse an integer from at most `width` chars of s (width < 0: unlimited).
 * Returns chars consumed, 0 if no digits. *neg reports a leading '-'. */
static size_t parse_uint(const char *s, int width, int base, unsigned long long *out, bool *neg) {
    size_t i = 0, lim = width < 0 ? SIZE_MAX : (size_t)width;
    *neg = false;
    if (i < lim && (s[i] == '+' || s[i] == '-')) {
        *neg = s[i++] == '-';
    }
    if ((base == 0 || base == 16) && i + 1 < lim && s[i] == '0' && (s[i + 1] | 0x20) == 'x' &&
        digit_val((unsigned char)s[i + 2]) < 16 && i + 2 < lim) {
        i += 2;
        base = 16;
    } else if (base == 0) {
        base = s[i] == '0' ? 8 : 10;
    }
    size_t start = i;
    unsigned long long v = 0;
    for (; i < lim && digit_val((unsigned char)s[i]) < base; i++) {
        v = v * (unsigned)base + (unsigned)digit_val((unsigned char)s[i]);
    }
    if (i == start) {
        return 0;
    }
    *out = v;
    return i;
}

unsigned long LIBC(strtoul)(const char *s, char **end, int base) {
    const char *p = s;
    while (LIBC(isspace)((unsigned char)*p)) {
        p++;
    }
    unsigned long long v = 0;
    bool neg;
    size_t n = parse_uint(p, -1, base, &v, &neg);
    if (end) {
        *end = (char *)(n ? p + n : s);
    }
    return n ? (unsigned long)(neg ? -v : v) : 0;
}

double LIBC(strtod)(const char *s, char **end) {
    const char *p = s;
    while (LIBC(isspace)((unsigned char)*p)) {
        p++;
    }
    bool neg = false;
    if (*p == '+' || *p == '-') {
        neg = *p++ == '-';
    }
    double v = 0;
    int digits = 0, exp10 = 0;
    for (; LIBC(isdigit)((unsigned char)*p); p++, digits++) {
        v = v * 10 + (*p - '0');
    }
    if (*p == '.') {
        for (p++; LIBC(isdigit)((unsigned char)*p); p++, digits++) {
            v = v * 10 + (*p - '0');
            exp10--;
        }
    }
    if (!digits) {
        if (end) {
            *end = (char *)s;
        }
        return 0;
    }
    if ((*p | 0x20) == 'e') {
        const char *q = p + 1;
        bool eneg = false;
        if (*q == '+' || *q == '-') {
            eneg = *q++ == '-';
        }
        if (LIBC(isdigit)((unsigned char)*q)) {
            int e = 0;
            for (; LIBC(isdigit)((unsigned char)*q); q++) {
                e = e < 10000 ? e * 10 + (*q - '0') : e;
            }
            exp10 += eneg ? -e : e;
            p = q;
        }
    }
    /* Scale by 10^|exp10| (accurate enough for config values). */
    double scale = 1, base = 10;
    for (int e = exp10 < 0 ? -exp10 : exp10; e; e >>= 1, base *= base) {
        if (e & 1) {
            scale *= base;
        }
    }
    v = exp10 < 0 ? v / scale : v * scale;
    if (end) {
        *end = (char *)p;
    }
    return neg ? -v : v;
}

double LIBC(atof)(const char *s) {
    return LIBC(strtod)(s, NULL);
}

static void store_int(va_list *ap, int len, unsigned long long v) {
    switch (len) {
    case -2:
        *va_arg(*ap, char *) = (char)v;
        break;
    case -1:
        *va_arg(*ap, short *) = (short)v;
        break;
    case 1:
        *va_arg(*ap, long *) = (long)v;
        break;
    case 2:
        *va_arg(*ap, long long *) = (long long)v;
        break;
    default:
        *va_arg(*ap, int *) = (int)v;
    }
}

int LIBC(vsscanf)(const char *s, const char *fmt, va_list ap_in) {
    va_list ap;
    va_copy(ap, ap_in);
    const char *start = s;
    int assigned = 0;
    bool any_conversion = false;
    while (*fmt) {
        if (LIBC(isspace)((unsigned char)*fmt)) {
            while (LIBC(isspace)((unsigned char)*fmt)) {
                fmt++;
            }
            while (LIBC(isspace)((unsigned char)*s)) {
                s++;
            }
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%') {
                fmt++;
                while (LIBC(isspace)((unsigned char)*s)) {
                    s++;
                }
            }
            if (*s != *fmt) {
                break;
            }
            s++;
            fmt++;
            continue;
        }
        fmt++;
        bool suppress = *fmt == '*';
        fmt += suppress;
        int width = -1;
        if (LIBC(isdigit)((unsigned char)*fmt)) {
            width = 0;
            while (LIBC(isdigit)((unsigned char)*fmt)) {
                width = width * 10 + (*fmt++ - '0');
            }
        }
        int len = 0;
        while (*fmt == 'h' || *fmt == 'l') {
            len += *fmt++ == 'h' ? -1 : 1;
        }
        char conv = *fmt++;
        bool set[256] = {false};
        if (conv == '[') {
            /* Scanset: optional '^', a leading ']' is literal, ranges unsupported. */
            bool negate = *fmt == '^';
            fmt += negate;
            if (*fmt == ']') {
                set[(unsigned char)*fmt++] = true;
            }
            while (*fmt && *fmt != ']') {
                set[(unsigned char)*fmt++] = true;
            }
            if (!*fmt) {
                goto done; /* unterminated */
            }
            fmt++;
            if (negate) {
                for (int c = 1; c < 256; c++) {
                    set[c] = !set[c];
                }
            }
        }
        if (conv == 'n') {
            if (!suppress) {
                store_int(&ap, len, (unsigned long long)(s - start));
            }
            continue;
        }
        if (conv != 'c' && conv != '[') {
            while (LIBC(isspace)((unsigned char)*s)) {
                s++;
            }
        }
        if (!*s) {
            break; /* input failure */
        }
        any_conversion = true;
        int base;
        switch (conv) {
        case 'd':
        case 'u':
            base = 10;
            break;
        case 'i':
            base = 0;
            break;
        case 'x':
        case 'X':
            base = 16;
            break;
        case 'o':
            base = 8;
            break;
        case 's': {
            char *d = suppress ? NULL : va_arg(ap, char *);
            int n = 0;
            for (; *s && !LIBC(isspace)((unsigned char)*s) && (width < 0 || n < width); n++) {
                if (d) {
                    *d++ = *s;
                }
                s++;
            }
            if (d) {
                *d = 0;
                assigned++;
            }
            continue;
        }
        case '[': {
            char *d = suppress ? NULL : va_arg(ap, char *);
            int n = 0;
            for (; *s && set[(unsigned char)*s] && (width < 0 || n < width); n++) {
                if (d) {
                    *d++ = *s;
                }
                s++;
            }
            if (!n) {
                goto done; /* matching failure */
            }
            if (d) {
                *d = 0;
                assigned++;
            }
            continue;
        }
        case 'c': {
            char *d = suppress ? NULL : va_arg(ap, char *);
            int n = width < 0 ? 1 : width;
            if ((int)strnlen(s, (size_t)n) < n) {
                goto done;
            }
            if (d) {
                memcpy(d, s, (size_t)n);
                assigned++;
            }
            s += n;
            continue;
        }
        default:
            goto done; /* unsupported conversion */
        }
        unsigned long long v;
        bool neg;
        size_t used = parse_uint(s, width, base, &v, &neg);
        if (!used) {
            break; /* matching failure */
        }
        s += used;
        if (!suppress) {
            store_int(&ap, len, neg ? (unsigned long long)-(long long)v : v);
            assigned++;
        }
    }
done:
    va_end(ap);
    return assigned == 0 && !any_conversion && !*s ? -1 : assigned;
}

int LIBC(sscanf)(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = LIBC(vsscanf)(s, fmt, ap);
    va_end(ap);
    return r;
}
