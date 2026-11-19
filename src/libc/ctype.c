/* <ctype.h> for the "C" locale (ASCII). */
#include "libc_pure.h"

int LIBC(isdigit)(int c) {
    return c >= '0' && c <= '9';
}

int LIBC(islower)(int c) {
    return c >= 'a' && c <= 'z';
}

int LIBC(isupper)(int c) {
    return c >= 'A' && c <= 'Z';
}

int LIBC(isalpha)(int c) {
    return LIBC(islower)(c) || LIBC(isupper)(c);
}

int LIBC(isalnum)(int c) {
    return LIBC(isalpha)(c) || LIBC(isdigit)(c);
}

int LIBC(isxdigit)(int c) {
    return LIBC(isdigit)(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int LIBC(isspace)(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

int LIBC(isprint)(int c) {
    return c >= 0x20 && c < 0x7f;
}

int LIBC(ispunct)(int c) {
    return LIBC(isprint)(c) && c != ' ' && !LIBC(isalnum)(c);
}

int LIBC(tolower)(int c) {
    return LIBC(isupper)(c) ? c + ('a' - 'A') : c;
}

int LIBC(toupper)(int c) {
    return LIBC(islower)(c) ? c - ('a' - 'A') : c;
}
