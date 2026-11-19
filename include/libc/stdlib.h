/* Minimal <stdlib.h> for the UEFI build. Heap is src/efi/alloc.c. */
#ifndef UD_LIBC_STDLIB_H
#define UD_LIBC_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t n);
void *calloc(size_t count, size_t n);
void *realloc(void *p, size_t n);
void free(void *p);

/** Terminate the application through the single UEFI teardown path. */
_Noreturn void exit(int status);
_Noreturn void abort(void);

int abs(int x);
int atoi(const char *s);
double atof(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
double strtod(const char *s, char **end);

/** No environment: always NULL. */
char *getenv(const char *name);
/** No shell: always -1. */
int system(const char *cmd);

#endif
