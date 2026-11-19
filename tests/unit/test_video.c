#include "check.h"
#include "video/video_core.h"

#include <stdlib.h>

int main(void) {
    pixfmt bgr, rgb, r565;
    /* PixelBlueGreenRedReserved8BitPerColor: 0x00RRGGBB */
    CHECK_EQ(pixfmt_from_masks(0x00ff0000, 0x0000ff00, 0x000000ff, &bgr), 0);
    /* PixelRedGreenBlueReserved8BitPerColor: 0x00BBGGRR */
    CHECK_EQ(pixfmt_from_masks(0x000000ff, 0x0000ff00, 0x00ff0000, &rgb), 0);
    CHECK_EQ(pixfmt_from_masks(0xf800, 0x07e0, 0x001f, &r565), 0);
    pixfmt bad;
    CHECK_EQ(pixfmt_from_masks(0, 0xff00, 0xff, &bad), -1);
    CHECK_EQ(pixfmt_from_masks(0xff00ff, 0xff00, 0xff, &bad), -1); /* non-contiguous */

    uint8_t pal[256][3] = {{0}};
    pal[1][0] = 0x12, pal[1][1] = 0x34, pal[1][2] = 0x56;
    pal[2][0] = 0xff, pal[2][1] = 0xff, pal[2][2] = 0xff;
    uint32_t lut[256];
    palette_build_lut(pal, &bgr, lut);
    CHECK_EQ(lut[1], 0x00123456);
    palette_build_lut(pal, &rgb, lut);
    CHECK_EQ(lut[1], 0x00563412);
    palette_build_lut(pal, &r565, lut);
    CHECK_EQ(lut[2], 0xffff);
    CHECK_EQ(lut[1], (0x12 >> 3) << 11 | (0x34 >> 2) << 5 | (0x56 >> 3));

    /* Aspect correction: 200 rows -> 240k rows, every row 1 or 2 times at k=1. */
    for (unsigned k = 1; k <= 6; k++) {
        unsigned total = 0, lo = 99, hi = 0;
        for (unsigned y = 0; y < 200; y++) {
            unsigned r = scale_row_repeat(y, k, true);
            total += r;
            lo = r < lo ? r : lo;
            hi = r > hi ? r : hi;
        }
        CHECK_EQ(total, 240 * k);
        CHECK_EQ(scale_out_height(200, k, true), 240 * k);
        CHECK(hi - lo <= 1); /* evenly spread */
        CHECK_EQ(scale_out_height(200, k, false), 200 * k);
    }

    /* scale_frame: 2x2 source, k=2, no aspect, into a wider pitch. */
    uint8_t src[4] = {1, 2, 2, 1};
    palette_build_lut(pal, &bgr, lut);
    uint32_t dst[4 * 6];
    uint32_t line[4];
    for (int i = 0; i < 24; i++) {
        dst[i] = 0xdeadbeef;
    }
    scale_frame(src, 2, 2, lut, dst, 6, 2, false, line, NULL);
    uint32_t a = lut[1], b = lut[2], X = 0xdeadbeef;
    uint32_t want[24] = {a, a, b, b, X, X, a, a, b, b, X, X, b, b, a, a, X, X, b, b, a, a, X, X};
    CHECK(memcmp(dst, want, sizeof want) == 0); /* padding beyond width untouched */

    /* Full-size frame with aspect at k=5 writes exactly 240*5 rows. */
    uint8_t *frame = calloc(320 * 200, 1);
    uint32_t *fb = calloc((size_t)1600 * 1201, 4);
    uint32_t *ln = calloc(1600, 4);
    fb[1600 * 1200] = 0x12345678; /* sentinel row just below the image */
    lut[0] = 0x00abcdef;
    scale_frame(frame, 320, 200, lut, fb, 1600, 5, true, ln, NULL);
    CHECK_EQ(fb[1600 * 1199 + 1599], 0x00abcdef);
    CHECK_EQ(fb[1600 * 1200], 0x12345678);
    free(frame), free(fb), free(ln);

    /* scale_for */
    bool asp = true;
    CHECK_EQ(scale_for(1920, 1080, 320, 200, 0, &asp), 4); /* 1280x960 fits; 5 needs 1200 rows */
    CHECK(asp);
    asp = false;
    CHECK_EQ(scale_for(1920, 1080, 320, 200, 0, &asp), 5); /* 1600x1000 square */
    asp = true;
    CHECK_EQ(scale_for(1920, 1200, 320, 200, 3, &asp), 3); /* capped */
    asp = true;
    CHECK_EQ(scale_for(320, 200, 320, 200, 0, &asp), 1); /* falls back to square pixels */
    CHECK(!asp);
    asp = true;
    CHECK_EQ(scale_for(300, 200, 320, 200, 0, &asp), 0);

    /* video_choose */
    mode_desc modes[] = {
        {640, 480, 640, true},
        {1920, 1200, 1920, true},
        {800, 600, 800, true},
        {320, 200, 320, true},
    };
    mode_choice c;
    CHECK_EQ(video_choose(modes, 4, 2, 0, 0, 320, 200, 0, true, &c), 0);
    CHECK_EQ(c.mode, 2); /* current mode kept */
    CHECK_EQ(c.k, 2);
    CHECK_EQ(c.x0, 80);
    CHECK_EQ(c.y0, 60);
    CHECK_EQ(video_choose(modes, 4, 3, 0, 0, 320, 200, 0, true, &c), 0);
    CHECK_EQ(c.mode, 1); /* current too small: largest */
    CHECK_EQ(c.k, 5);
    CHECK_EQ(video_choose(modes, 4, 1, 640, 480, 320, 200, 0, true, &c), 0);
    CHECK_EQ(c.mode, 0); /* preferred */
    mode_desc bltonly[] = {{1024, 768, 1024, false}};
    CHECK_EQ(video_choose(bltonly, 1, 0, 0, 0, 320, 200, 0, true, &c), 0); /* Blt-only OK if sole */
    mode_desc mixed[] = {{1024, 768, 1024, false}, {800, 600, 800, true}};
    CHECK_EQ(video_choose(mixed, 2, 0, 0, 0, 320, 200, 0, true, &c), 0);
    CHECK_EQ(c.mode, 1); /* framebuffer mode preferred */
    mode_desc broken[] = {{0, 0, 0, true}, {800, 600, 400, true}};
    CHECK_EQ(video_choose(broken, 2, 0, 0, 0, 320, 200, 0, true, &c), -1);

    return CHECK_DONE();
}
