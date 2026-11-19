#include "check.h"
#include "input/pointer_core.h"

static int feed(ps2mouse_dec *d, const uint8_t *b, int n, int *bt, int *dx, int *dy) {
    int pkts = 0;
    for (int i = 0; i < n; i++) {
        pkts += ps2mouse_byte(d, b[i], bt, dx, dy);
    }
    return pkts;
}

int main(void) {
    ps2mouse_dec d = {0};
    int bt, dx, dy;

    /* Left button, +5 right, +3 up. */
    CHECK_EQ(feed(&d, (const uint8_t[]){0x09, 5, 3}, 3, &bt, &dx, &dy), 1);
    CHECK_EQ(bt, 1);
    CHECK_EQ(dx, 5);
    CHECK_EQ(dy, 3);
    /* Negative motion via the sign bits: -2 left, -4 down. */
    CHECK_EQ(feed(&d, (const uint8_t[]){0x38, 0xfe, 0xfc}, 3, &bt, &dx, &dy), 1);
    CHECK_EQ(bt, 0);
    CHECK_EQ(dx, -2);
    CHECK_EQ(dy, -4);
    /* Junk before a packet is skipped (bit 3 clear). */
    CHECK_EQ(feed(&d, (const uint8_t[]){0x00, 0x05, 0x0a, 1, 1}, 5, &bt, &dx, &dy), 1);
    CHECK_EQ(bt, 2);
    /* X overflow: that axis reports 0. */
    CHECK_EQ(feed(&d, (const uint8_t[]){0x48, 0x7f, 2}, 3, &bt, &dx, &dy), 1);
    CHECK_EQ(dx, 0);
    CHECK_EQ(dy, 2);

    CHECK_EQ(pointer_scale_simple(32, 32), 16);
    CHECK_EQ(pointer_scale_simple(-8, 4), -32);
    CHECK_EQ(pointer_scale_simple(7, 0), 7);

    absptr_track t = {0};
    absptr_motion(&t, true, 500, 500, 0, 1000, 0, 1000, &dx, &dy);
    CHECK(dx == 0 && dy == 0); /* touch start: no jump */
    absptr_motion(&t, true, 600, 450, 0, 1000, 0, 1000, &dx, &dy);
    CHECK_EQ(dx, 120); /* a tenth of the width */
    CHECK_EQ(dy, 60);  /* moved up the pad */
    absptr_motion(&t, false, 900, 900, 0, 1000, 0, 1000, &dx, &dy);
    CHECK(dx == 0 && dy == 0);
    absptr_motion(&t, true, 100, 100, 0, 1000, 0, 1000, &dx, &dy);
    CHECK(dx == 0 && dy == 0); /* lifted and touched elsewhere: no jump */
    absptr_motion(&t, true, 90, 100, 0, 0, 0, 0, &dx, &dy);
    CHECK(dx == 0 && dy == 0); /* no range reported */
    return CHECK_DONE();
}
