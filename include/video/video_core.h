/* Video logic with no firmware access: pixel formats, palette LUT, integer
 * scaler with 4:3 aspect correction, and mode choice. Unit-tested on the host.
 * See docs/architecture.md#video-video. */
#ifndef UD_VIDEO_CORE_H
#define UD_VIDEO_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Where each 8-bit colour channel lands in a 32-bit pixel. */
typedef struct {
    uint8_t r_shift, r_bits, g_shift, g_bits, b_shift, b_bits;
} pixfmt;

/** Derive a pixfmt from channel masks. Returns 0, or -1 for empty or non-contiguous masks. */
int pixfmt_from_masks(uint32_t r_mask, uint32_t g_mask, uint32_t b_mask, pixfmt *out);

/** Build the 256-entry LUT for `fmt` from 8-bit RGB triplets. */
void palette_build_lut(const uint8_t rgb[256][3], const pixfmt *fmt, uint32_t lut[256]);

/** Output height for `src_h` rows at integer scale k (aspect: 6/5 taller). */
unsigned scale_out_height(unsigned src_h, unsigned k, bool aspect);

/** How many output rows source row `y` becomes. Sums to scale_out_height. */
unsigned scale_row_repeat(unsigned y, unsigned k, bool aspect);

/**
 * Scale and convert an indexed `src_w` x `src_h` frame into 32-bit `dst`
 * (row pitch `pitch_px` pixels). Each output row is built once in `line`
 * (capacity src_w * k) and copied for its repeats: one sequential write pass,
 * no reads of `dst` (which may be an uncached framebuffer). `copy` writes each
 * row (NULL = memcpy; use a wide-store copy for uncached memory, fb_copy.h).
 */
void scale_frame(const uint8_t *src, unsigned src_w, unsigned src_h, const uint32_t lut[256],
                 uint32_t *dst, size_t pitch_px, unsigned k, bool aspect, uint32_t *line,
                 void (*copy)(uint32_t *dst, const uint32_t *src, size_t n_px));

typedef struct {
    uint32_t w, h, pitch; /* pitch = PixelsPerScanLine */
    bool has_fb;          /* false for PixelBltOnly */
} mode_desc;

typedef struct {
    int mode;    /* index into the mode list */
    unsigned k;  /* integer scale */
    bool aspect; /* 200 -> 240 line correction applied */
    uint32_t x0; /* top-left of the image within the mode */
    uint32_t y0;
} mode_choice;

/**
 * Largest k (capped at max_scale; 0 = no cap) with src_w*k <= w and
 * out_height <= h. If even k = 1 with aspect does not fit, retries without
 * aspect; returns 0 if nothing fits.
 */
unsigned scale_for(uint32_t w, uint32_t h, unsigned src_w, unsigned src_h, unsigned max_scale,
                   bool *aspect);

/**
 * Choose a mode: `pref_w x pref_h` if given and present; else the current mode
 * if it is at least 640x480; else the largest. Modes without a framebuffer are
 * used only if no mode has one. Returns 0, or -1 if no mode is usable.
 */
int video_choose(const mode_desc *modes, int n, int current, uint32_t pref_w, uint32_t pref_h,
                 unsigned src_w, unsigned src_h, unsigned max_scale, bool aspect, mode_choice *out);

#endif
