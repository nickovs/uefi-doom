/* Heap on UEFI pool memory, tracked so everything can be freed at exit (UEFI
 * does not reclaim an application's allocations). malloc/calloc/realloc/free
 * are provided by alloc.c. */
#ifndef UD_EFI_ALLOC_H
#define UD_EFI_ALLOC_H

#include <stddef.h>

/** Number of live heap blocks (tests assert 0 after teardown). */
size_t alloc_live_count(void);

/** Free every live heap block. Called last, after the log has been flushed. */
void alloc_free_all(void);

/**
 * Allocate `bytes` rounded up to whole pages (EfiLoaderData), 4 KiB aligned.
 * Freed automatically at exit (cleanup registry). Returns NULL on failure.
 */
void *alloc_pages(size_t bytes);

#endif
