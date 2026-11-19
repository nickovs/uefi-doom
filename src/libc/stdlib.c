/* stdlib pieces for the UEFI build. malloc/free live in src/efi/alloc.c. */
#include <stdlib.h>

#include "efi/efi_main.h"

/* Referenced by clang's Windows target whenever floating point is used. */
int _fltused = 0;

_Noreturn void exit(int status) {
    efi_exit(status == 0 ? EXIT_REASON_QUIT : EXIT_REASON_FATAL);
}

_Noreturn void abort(void) {
    efi_exit(EXIT_REASON_FATAL);
}

long strtol(const char *s, char **end, int base) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    int neg = 0;
    if (*s == '+' || *s == '-') {
        neg = *s++ == '-';
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }
    long v = 0;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') {
            d = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            d = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            d = *s - 'A' + 10;
        } else {
            break;
        }
        if (d >= base) {
            break;
        }
        v = v * base + d;
    }
    if (end) {
        *end = (char *)s;
    }
    return neg ? -v : v;
}

int atoi(const char *s) {
    return (int)strtol(s, NULL, 10);
}
