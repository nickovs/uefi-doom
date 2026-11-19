/* Minimal <stdio.h> for the UEFI build. Formatting is third_party/printf;
 * stdout/stderr go to the log (src/libc/stdio.c); FILE wraps efi/fs
 * (src/libc/file.c). See docs/architecture.md#c-library-libc. */
#ifndef UD_LIBC_STDIO_H
#define UD_LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct ud_file FILE;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int puts(const char *s);
int putchar(int c);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *buf, size_t size, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fflush(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fgetc(FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
char *fgets(char *buf, int n, FILE *f);
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vfprintf(FILE *f, const char *fmt, va_list ap);
int fscanf(FILE *f, const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
int vsscanf(const char *s, const char *fmt, va_list ap);
int fileno(FILE *f);

int remove(const char *path);
int rename(const char *from, const char *to);

#endif
