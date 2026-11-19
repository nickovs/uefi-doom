/* stdio for the UEFI build: formatting by third_party/printf, console output
 * routed to the log at INFO level. FILE streams are in file.c. */
#include <stdio.h>
#include <string.h>

#include <printf/printf.h>

#include "efi/log.h"

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    return vsnprintf_(buf, n, fmt, ap);
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf_(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf_(buf, (size_t)-1 >> 1, fmt, ap);
    va_end(ap);
    return r;
}

int vprintf(const char *fmt, va_list ap) {
    char line[512];
    int r = vsnprintf_(line, sizeof line, fmt, ap);
    log_write(LOG_INFO, line, strnlen(line, sizeof line));
    return r;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int puts(const char *s) {
    log_write(LOG_INFO, s, strlen(s));
    log_write(LOG_INFO, "\n", 1);
    return 1;
}

int putchar(int c) {
    char ch = (char)c;
    log_write(LOG_INFO, &ch, 1);
    return (unsigned char)c;
}

/* Required by third_party/printf for printf_(); we never call it, but route it
 * sensibly anyway. */
void putchar_(char c) {
    log_write(LOG_INFO, &c, 1);
}
