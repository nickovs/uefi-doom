/* Pointer decoding with no firmware access (pure, host-tested): PS/2 mouse
 * packets, and conversion of firmware pointer states (SIMPLE_POINTER counts,
 * ABSOLUTE_POINTER positions) into relative mouse motion. */
#ifndef UD_INPUT_POINTER_CORE_H
#define UD_INPUT_POINTER_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* Mouse counts per millimetre that motion is normalised to (a typical
 * 400 counts/inch USB mouse), so every source turns at a similar rate. */
#define POINTER_COUNTS_PER_MM 16
/* Counts for a finger moving the full width of an absolute device. */
#define POINTER_ABS_FULL_SWEEP 1200

/* --- PS/2 mouse (3-byte standard packets) -------------------------------- */

typedef struct {
    uint8_t pkt[3];
    uint8_t n;
} ps2mouse_dec;

/**
 * Feed one aux byte. Returns true when a packet completed, with buttons (bit 0
 * left, 1 right, 2 middle) and motion (dy positive = away from the user).
 * Bytes before a valid first byte (bit 3 set) are dropped to resynchronise;
 * overflowed axes report 0.
 */
bool ps2mouse_byte(ps2mouse_dec *d, uint8_t b, int *buttons, int *dx, int *dy);

/* --- Firmware pointers ---------------------------------------------------- */

/** Scale a SIMPLE_POINTER relative count at `res` counts/mm (0 = unknown). */
int pointer_scale_simple(int32_t rel, uint64_t res);

typedef struct {
    bool touching;
    uint64_t x, y;
} absptr_track;

/**
 * Turn an ABSOLUTE_POINTER state into motion: while the touch is held,
 * consecutive positions give a delta scaled so that sweeping [min, max]
 * yields POINTER_ABS_FULL_SWEEP counts; a new touch gives no motion.
 */
void absptr_motion(absptr_track *t, bool touch, uint64_t x, uint64_t y, uint64_t min_x,
                   uint64_t max_x, uint64_t min_y, uint64_t max_y, int *dx, int *dy);

#endif
