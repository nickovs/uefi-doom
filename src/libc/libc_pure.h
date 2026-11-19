/* Declarations of the libc shim's pure (host-testable) functions.
 *
 * Each pure source defines its functions as LIBC(name). In the UEFI build that
 * is just `name`; host unit tests compile with -DLIBC_HOST_TEST, which makes it
 * shim_<name> so the shim can be checked next to the host libc.
 * See docs/architecture.md#c-library-libc. */
#ifndef UD_LIBC_PURE_H
#define UD_LIBC_PURE_H

#include <stdarg.h>
#include <stddef.h>

#ifdef LIBC_HOST_TEST
#define LIBC(name) shim_##name
#else
#define LIBC(name) name
#endif

/* ctype.c */
int LIBC(isalnum)(int c);
int LIBC(isalpha)(int c);
int LIBC(isdigit)(int c);
int LIBC(islower)(int c);
int LIBC(isprint)(int c);
int LIBC(ispunct)(int c);
int LIBC(isspace)(int c);
int LIBC(isupper)(int c);
int LIBC(isxdigit)(int c);
int LIBC(tolower)(int c);
int LIBC(toupper)(int c);

/* str.c */
char *LIBC(strcpy)(char *dst, const char *src);
char *LIBC(strcat)(char *dst, const char *src);
char *LIBC(strdup)(const char *s);
int LIBC(strcasecmp)(const char *a, const char *b);
int LIBC(strncasecmp)(const char *a, const char *b, size_t n);
char *LIBC(strerror)(int err);
int LIBC(abs)(int x);

/* scan_core.c */
unsigned long LIBC(strtoul)(const char *s, char **end, int base);
double LIBC(strtod)(const char *s, char **end);
double LIBC(atof)(const char *s);
int LIBC(vsscanf)(const char *s, const char *fmt, va_list ap);
int LIBC(sscanf)(const char *s, const char *fmt, ...);

/* file.c (over efi/fs.h and efi/log.h) */
struct ud_file;
extern struct ud_file *LIBC(stdout);
extern struct ud_file *LIBC(stderr);
struct ud_file *LIBC(fopen)(const char *path, const char *mode);
int LIBC(fclose)(struct ud_file *f);
size_t LIBC(fread)(void *buf, size_t size, size_t n, struct ud_file *f);
size_t LIBC(fwrite)(const void *buf, size_t size, size_t n, struct ud_file *f);
int LIBC(fseek)(struct ud_file *f, long off, int whence);
long LIBC(ftell)(struct ud_file *f);
int LIBC(fflush)(struct ud_file *f);
int LIBC(feof)(struct ud_file *f);
int LIBC(ferror)(struct ud_file *f);
int LIBC(fgetc)(struct ud_file *f);
int LIBC(fputc)(int c, struct ud_file *f);
int LIBC(fputs)(const char *s, struct ud_file *f);
char *LIBC(fgets)(char *buf, int n, struct ud_file *f);
int LIBC(fscanf)(struct ud_file *f, const char *fmt, ...);
int LIBC(fprintf)(struct ud_file *f, const char *fmt, ...);
int LIBC(vfprintf)(struct ud_file *f, const char *fmt, va_list ap);
int LIBC(fileno)(struct ud_file *f);
int LIBC(remove)(const char *path);
int LIBC(rename)(const char *from, const char *to);

#endif
