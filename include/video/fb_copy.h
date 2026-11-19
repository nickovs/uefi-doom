/* Row copies into the framebuffer. On this hardware the framebuffer is
 * uncached (UC): every store is a separate bus transaction of roughly fixed
 * latency, so throughput scales with store width, and `rep movsb` (memcpy)
 * degrades to one byte per transaction. Measured on a laptop: 6.8 MB/s
 * for rep movsb, 27 MB/s for 32-bit, 109 MB/s for 128-bit stores. */
#ifndef UD_VIDEO_FB_COPY_H
#define UD_VIDEO_FB_COPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*row_copy_fn)(uint32_t *dst, const uint32_t *src, size_t n_px);

/** 128-bit aligned stores (SSE2: always available on x86_64). */
void fb_copy_sse(uint32_t *dst, const uint32_t *src, size_t n_px);

/** 256-bit aligned stores. Only call if fb_avx_usable(). */
void fb_copy_avx(uint32_t *dst, const uint32_t *src, size_t n_px);

/** True if the CPU has AVX and the firmware enabled YMM state (CR4.OSXSAVE + XCR0). */
bool fb_avx_usable(void);

/** Widest usable copy, and its name for logs. */
row_copy_fn fb_copy_best(const char **name);

#endif
