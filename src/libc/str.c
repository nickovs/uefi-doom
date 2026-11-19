/* String functions beyond the memory primitives in string.c (pure C). */
#include <stdlib.h>
#include <string.h>

#include "libc_pure.h"

char *LIBC(strcpy)(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {
    }
    return dst;
}

char *LIBC(strcat)(char *dst, const char *src) {
    LIBC(strcpy)(dst + strlen(dst), src);
    return dst;
}

char *LIBC(strdup)(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    return p ? memcpy(p, s, n) : NULL;
}

int LIBC(strncasecmp)(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int x = LIBC(tolower)((unsigned char)a[i]), y = LIBC(tolower)((unsigned char)b[i]);
        if (x != y || !x) {
            return x - y;
        }
    }
    return 0;
}

int LIBC(strcasecmp)(const char *a, const char *b) {
    return LIBC(strncasecmp)(a, b, (size_t)-1);
}

char *LIBC(strerror)(int err) {
    static char buf[24];
    static const char digits[] = "0123456789";
    /* Messages are not needed; "error N" is enough for logs. */
    char tmp[12];
    int n = 0;
    unsigned u = err < 0 ? (unsigned)-err : (unsigned)err;
    do {
        tmp[n++] = digits[u % 10];
        u /= 10;
    } while (u);
    char *p = LIBC(strcpy)(buf, err < 0 ? "error -" : "error ") + (err < 0 ? 7 : 6);
    while (n) {
        *p++ = tmp[--n];
    }
    *p = 0;
    return buf;
}

int LIBC(abs)(int x) {
    return x < 0 ? -x : x;
}
