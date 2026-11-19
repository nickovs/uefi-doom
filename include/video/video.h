/* GOP video output: mode selection, palette LUT, scale + present.
 * See docs/architecture.md#video-video. */
#ifndef UD_VIDEO_H
#define UD_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include "video/video_core.h"

#define VIDEO_SRC_W 320
#define VIDEO_SRC_H 200

typedef struct {
    unsigned max_scale;      /* 0 = unlimited */
    bool aspect_correct;     /* default true */
    uint32_t pref_w, pref_h; /* 0 = no preference */
    bool force_blt;          /* present via GOP->Blt even if a framebuffer exists */
    bool allow_wc;           /* map the framebuffer write-combining via PAT (port: true) */
    bool allow_avx;          /* enable AVX state for 256-bit framebuffer stores (port: true) */
} video_config;

typedef struct {
    uint32_t mode, w, h, pitch;
    int gop_format;            /* EFI_GRAPHICS_PIXEL_FORMAT */
    uint64_t fb_base, fb_size; /* 0 for Blt-only modes */
    unsigned k;
    bool aspect;
    uint32_t x0, y0, out_w, out_h;
    bool use_blt;
    bool wc; /* framebuffer mapped write-combining */
} video_info;

/** Locate GOP, pick and set a mode, clear the screen. Registers restore at exit. */
bool video_init(const video_config *cfg);

/** Convert, scale and display a 320x200 indexed frame. */
void video_present(const uint8_t *frame, const uint8_t rgb[256][3], bool palette_changed);

/** Restore the original mode and text console (also runs at exit). */
void video_shutdown(void);

/** Current mode details (valid after video_init). */
const video_info *video_get_info(void);

/** The GOP interface (EFI_GRAPHICS_OUTPUT_PROTOCOL *), or NULL; for HWPROBE. */
void *video_gop(void);

#endif
