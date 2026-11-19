/* Wide-store framebuffer row copies. Inline asm keeps the compiler from
 * turning the loops back into a memcpy (rep movsb) call. */
#include "video/fb_copy.h"

#include "efi/cpu.h"

/* Copy pixels one at a time until dst is `align`-byte aligned; returns pixels done. */
static size_t head(uint32_t *dst, const uint32_t *src, size_t n, uintptr_t align) {
    size_t i = 0;
    while (i < n && ((uintptr_t)(dst + i) & (align - 1))) {
        *(volatile uint32_t *)(dst + i) = src[i];
        i++;
    }
    return i;
}

static void tail(uint32_t *dst, const uint32_t *src, size_t from, size_t n) {
    for (size_t i = from; i < n; i++) {
        *(volatile uint32_t *)(dst + i) = src[i];
    }
}

void fb_copy_sse(uint32_t *dst, const uint32_t *src, size_t n) {
    size_t i = head(dst, src, n, 16);
    size_t blocks = (n - i) / 4;
    const uint32_t *s = src + i;
    uint32_t *d = dst + i;
    for (size_t b = 0; b < blocks; b++, s += 4, d += 4) {
        __asm__ volatile("movdqu (%1), %%xmm0\n\tmovdqa %%xmm0, (%0)"
                         :
                         : "r"(d), "r"(s)
                         : "xmm0", "memory");
    }
    tail(dst, src, i + blocks * 4, n);
}

void fb_copy_avx(uint32_t *dst, const uint32_t *src, size_t n) {
    size_t i = head(dst, src, n, 32);
    size_t blocks = (n - i) / 8;
    const uint32_t *s = src + i;
    uint32_t *d = dst + i;
    for (size_t b = 0; b < blocks; b++, s += 8, d += 8) {
        __asm__ volatile("vmovdqu (%1), %%ymm0\n\tvmovdqa %%ymm0, (%0)"
                         :
                         : "r"(d), "r"(s)
                         : "xmm0", "memory");
    }
    __asm__ volatile("vzeroupper" ::: "memory");
    tail(dst, src, i + blocks * 8, n);
}

bool fb_avx_usable(void) {
    uint32_t r[4];
    cpuid(1, 0, r);
    bool avx = (r[2] >> 28) & 1, osxsave = (r[2] >> 27) & 1;
    if (!avx || !osxsave) {
        return false;
    }
    uint32_t lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (lo & 6) == 6; /* XMM and YMM state enabled */
}

row_copy_fn fb_copy_best(const char **name) {
    if (fb_avx_usable()) {
        *name = "avx-256";
        return fb_copy_avx;
    }
    *name = "sse-128";
    return fb_copy_sse;
}
