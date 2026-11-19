/* Set-1 decoder. See include/input/i8042_core.h. */
#include "input/i8042_core.h"

#include "input/keymap_core.h"

int i8042_dec_byte(i8042_dec *d, uint8_t b, key_event out[2]) {
    if (d->e1_left) {
        /* Pause is E1 1D 45 E1 9D C5 and has no release: report a tap at the end. */
        if (--d->e1_left == 0) {
            out[0] = (key_event){1, DK_PAUSE};
            out[1] = (key_event){0, DK_PAUSE};
            return 2;
        }
        return 0;
    }
    switch (b) {
    case 0xe0:
        d->e0 = 1;
        return 0;
    case 0xe1:
        d->e1_left = 5;
        return 0;
    case 0x00: /* key detection error / overrun */
    case 0xfa: /* ACK */
    case 0xfe: /* resend */
    case 0xff: /* error / overrun */
    case 0xee: /* echo */
        d->e0 = 0;
        return 0;
    default:
        break;
    }
    uint8_t key = keymap_set1(b & 0x7f, d->e0);
    d->e0 = 0;
    if (!key) {
        return 0;
    }
    out[0] = (key_event){(b & 0x80) ? 0 : 1, key};
    return 1;
}
