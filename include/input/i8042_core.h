/* PS/2 keyboard scancode decoder (translated set 1; pure, host-tested).
 * Handles E0-prefixed keys, the E1 Pause sequence and controller noise. */
#ifndef UD_INPUT_I8042_CORE_H
#define UD_INPUT_I8042_CORE_H

#include <stdint.h>

#include "input/doomkey.h"

typedef struct {
    uint8_t e0;      /* last byte was an E0 prefix */
    uint8_t e1_left; /* bytes still to skip in an E1 (Pause) sequence */
} i8042_dec;

/** Feed one byte from port 0x60. Writes up to 2 events to `out`; returns the count. */
int i8042_dec_byte(i8042_dec *d, uint8_t b, key_event out[2]);

#endif
