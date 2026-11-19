/* Minimal <assert.h> for the UEFI build: a failed assertion is fatal. */
#ifndef UD_LIBC_ASSERT_H
#define UD_LIBC_ASSERT_H

_Noreturn void __assert_fail(const char *expr, const char *file, int line);

#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__))
#endif

#endif
