/* Tracked heap over gBS->AllocatePool, plus page allocations.
 * See docs/architecture.md#memory. */
#include "efi/alloc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "efi/efi.h"
#include "efi/efi_main.h"
#include "efi/log.h"

#define BLK_MAGIC 0x4b4c4244u /* "DBLK" */
#define BLK_DEAD 0xdeadb10cu
#define ALIGN 16

/* Header placed immediately before each user pointer; 48 bytes keeps the user
 * pointer 16-byte aligned relative to the header. */
typedef struct blk {
    struct blk *prev, *next;
    size_t size;
    void *raw; /* pointer returned by AllocatePool */
    uint32_t magic;
    uint32_t pad[3];
} blk;

_Static_assert(sizeof(blk) % ALIGN == 0, "header must preserve alignment");

static blk *live;
static size_t live_count;

static blk *hdr(void *p) {
    blk *b = (blk *)p - 1;
    if (b->magic != BLK_MAGIC) {
        log_msg(LOG_ERROR, "heap corruption or bad free at %p (magic %08x)", p, b->magic);
        efi_exit(EXIT_REASON_FATAL);
    }
    return b;
}

void *malloc(size_t n) {
    void *raw = NULL;
    if (n > SIZE_MAX - sizeof(blk) - ALIGN) {
        return NULL;
    }
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, n + sizeof(blk) + ALIGN, &raw))) {
        return NULL;
    }
    uintptr_t user = ((uintptr_t)raw + sizeof(blk) + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);
    blk *b = (blk *)user - 1;
    b->raw = raw;
    b->size = n;
    b->magic = BLK_MAGIC;
    b->prev = NULL;
    b->next = live;
    if (live) {
        live->prev = b;
    }
    live = b;
    live_count++;
    return (void *)user;
}

void free(void *p) {
    if (!p) {
        return;
    }
    blk *b = hdr(p);
    if (b->prev) {
        b->prev->next = b->next;
    } else {
        live = b->next;
    }
    if (b->next) {
        b->next->prev = b->prev;
    }
    b->magic = BLK_DEAD;
    live_count--;
    gBS->FreePool(b->raw);
}

void *calloc(size_t count, size_t n) {
    if (n && count > SIZE_MAX / n) {
        return NULL;
    }
    void *p = malloc(count * n);
    if (p) {
        memset(p, 0, count * n);
    }
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) {
        return malloc(n);
    }
    if (n == 0) {
        free(p);
        return NULL;
    }
    blk *b = hdr(p);
    void *q = malloc(n);
    if (q) {
        memcpy(q, p, b->size < n ? b->size : n);
        free(p);
    }
    return q;
}

size_t alloc_live_count(void) {
    return live_count;
}

void alloc_free_all(void) {
    while (live) {
        free((void *)(live + 1));
    }
}

/* --- pages ---------------------------------------------------------------- */

typedef struct {
    EFI_PHYSICAL_ADDRESS addr;
    UINTN pages;
} page_alloc;

static void pages_cleanup(void *ctx) {
    page_alloc *pa = ctx;
    gBS->FreePages(pa->addr, pa->pages);
    free(pa);
}

void *alloc_pages(size_t bytes) {
    page_alloc *pa = malloc(sizeof *pa);
    if (!pa) {
        return NULL;
    }
    pa->pages = EFI_SIZE_TO_PAGES(bytes);
    if (EFI_ERROR(gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pa->pages, &pa->addr))) {
        free(pa);
        return NULL;
    }
    efi_cleanup_push(pages_cleanup, pa, "pages");
    return (void *)(uintptr_t)pa->addr;
}
