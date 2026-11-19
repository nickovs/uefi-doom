#include "video/video_core.h"

#include <string.h>

static int mask_to_field(uint32_t mask, uint8_t *shift, uint8_t *bits) {
    if (!mask) {
        return -1;
    }
    uint8_t s = 0;
    while (!(mask & 1)) {
        mask >>= 1;
        s++;
    }
    uint8_t b = 0;
    while (mask & 1) {
        mask >>= 1;
        b++;
    }
    if (mask) {
        return -1; /* non-contiguous */
    }
    *shift = s;
    *bits = b;
    return 0;
}

int pixfmt_from_masks(uint32_t r_mask, uint32_t g_mask, uint32_t b_mask, pixfmt *out) {
    if (mask_to_field(r_mask, &out->r_shift, &out->r_bits) < 0 ||
        mask_to_field(g_mask, &out->g_shift, &out->g_bits) < 0 ||
        mask_to_field(b_mask, &out->b_shift, &out->b_bits) < 0) {
        return -1;
    }
    return 0;
}

static uint32_t channel(uint8_t v, uint8_t shift, uint8_t bits) {
    uint32_t x = bits >= 8 ? (uint32_t)v << (bits - 8) : (uint32_t)v >> (8 - bits);
    return x << shift;
}

void palette_build_lut(const uint8_t rgb[256][3], const pixfmt *f, uint32_t lut[256]) {
    for (int i = 0; i < 256; i++) {
        lut[i] = channel(rgb[i][0], f->r_shift, f->r_bits) |
                 channel(rgb[i][1], f->g_shift, f->g_bits) |
                 channel(rgb[i][2], f->b_shift, f->b_bits);
    }
}

unsigned scale_out_height(unsigned src_h, unsigned k, bool aspect) {
    return aspect ? src_h * 6 * k / 5 : src_h * k;
}

unsigned scale_row_repeat(unsigned y, unsigned k, bool aspect) {
    if (!aspect) {
        return k;
    }
    /* Bresenham-style: rows spread the extra 1/5 evenly. */
    return (y + 1) * 6 * k / 5 - y * 6 * k / 5;
}

void scale_frame(const uint8_t *src, unsigned src_w, unsigned src_h, const uint32_t lut[256],
                 uint32_t *dst, size_t pitch_px, unsigned k, bool aspect, uint32_t *line,
                 void (*copy)(uint32_t *dst, const uint32_t *src, size_t n_px)) {
    size_t out_w = (size_t)src_w * k;
    for (unsigned y = 0; y < src_h; y++) {
        const uint8_t *s = src + (size_t)y * src_w;
        uint32_t *l = line;
        for (unsigned x = 0; x < src_w; x++) {
            uint32_t px = lut[s[x]];
            for (unsigned i = 0; i < k; i++) {
                *l++ = px;
            }
        }
        for (unsigned r = scale_row_repeat(y, k, aspect); r; r--) {
            if (copy) {
                copy(dst, line, out_w);
            } else {
                memcpy(dst, line, out_w * sizeof *line);
            }
            dst += pitch_px;
        }
    }
}

unsigned scale_for(uint32_t w, uint32_t h, unsigned src_w, unsigned src_h, unsigned max_scale,
                   bool *aspect) {
    for (int pass = 0; pass < 2; pass++) {
        unsigned k = w / src_w;
        while (k && scale_out_height(src_h, k, *aspect) > h) {
            k--;
        }
        if (max_scale && k > max_scale) {
            k = max_scale;
        }
        if (k) {
            return k;
        }
        if (!*aspect) {
            break;
        }
        *aspect = false; /* too small for corrected 1x: try square pixels */
    }
    return 0;
}

static bool usable(const mode_desc *m, bool need_fb) {
    return m->w && m->h && m->pitch >= m->w && (!need_fb || m->has_fb);
}

int video_choose(const mode_desc *modes, int n, int current, uint32_t pref_w, uint32_t pref_h,
                 unsigned src_w, unsigned src_h, unsigned max_scale, bool aspect,
                 mode_choice *out) {
    bool any_fb = false;
    for (int i = 0; i < n; i++) {
        any_fb |= usable(&modes[i], true);
    }
    int pick = -1;
    if (pref_w && pref_h) {
        for (int i = 0; i < n && pick < 0; i++) {
            if (modes[i].w == pref_w && modes[i].h == pref_h && usable(&modes[i], any_fb)) {
                pick = i;
            }
        }
    }
    if (pick < 0 && current >= 0 && current < n && usable(&modes[current], any_fb) &&
        modes[current].w >= 640 && modes[current].h >= 480) {
        pick = current;
    }
    if (pick < 0) {
        uint64_t best = 0;
        for (int i = 0; i < n; i++) {
            uint64_t area = (uint64_t)modes[i].w * modes[i].h;
            if (usable(&modes[i], any_fb) && area > best) {
                best = area;
                pick = i;
            }
        }
    }
    if (pick < 0) {
        return -1;
    }
    bool asp = aspect;
    unsigned k = scale_for(modes[pick].w, modes[pick].h, src_w, src_h, max_scale, &asp);
    if (!k) {
        return -1;
    }
    out->mode = pick;
    out->k = k;
    out->aspect = asp;
    out->x0 = (modes[pick].w - src_w * k) / 2;
    out->y0 = (modes[pick].h - scale_out_height(src_h, k, asp)) / 2;
    return 0;
}
