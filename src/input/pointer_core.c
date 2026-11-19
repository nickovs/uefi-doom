/* Pointer decoding. See include/input/pointer_core.h. */
#include "input/pointer_core.h"

bool ps2mouse_byte(ps2mouse_dec *d, uint8_t b, int *buttons, int *dx, int *dy) {
    if (d->n == 0 && !(b & 0x08)) {
        return false; /* not a first byte: resynchronise */
    }
    d->pkt[d->n++] = b;
    if (d->n < 3) {
        return false;
    }
    d->n = 0;
    uint8_t s = d->pkt[0];
    *buttons = s & 7;
    *dx = (s & 0x40) ? 0 : (int)d->pkt[1] - ((s & 0x10) ? 256 : 0);
    *dy = (s & 0x80) ? 0 : (int)d->pkt[2] - ((s & 0x20) ? 256 : 0);
    return true;
}

int pointer_scale_simple(int32_t rel, uint64_t res) {
    if (!res) {
        return rel;
    }
    return (int)((int64_t)rel * POINTER_COUNTS_PER_MM / (int64_t)res);
}

static int sweep(uint64_t from, uint64_t to, uint64_t min, uint64_t max) {
    uint64_t span = max > min ? max - min : 0;
    if (!span) {
        return 0;
    }
    int64_t d = (int64_t)(to - from);
    return (int)(d * POINTER_ABS_FULL_SWEEP / (int64_t)span);
}

void absptr_motion(absptr_track *t, bool touch, uint64_t x, uint64_t y, uint64_t min_x,
                   uint64_t max_x, uint64_t min_y, uint64_t max_y, int *dx, int *dy) {
    *dx = *dy = 0;
    if (touch && t->touching) {
        *dx = sweep(t->x, x, min_x, max_x);
        *dy = -sweep(t->y, y, min_y, max_y); /* screen y grows downwards */
    }
    t->touching = touch;
    t->x = x;
    t->y = y;
}
