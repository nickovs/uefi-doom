/* ConIn keymap and pulse model. See include/input/conin_core.h. */
#include "input/conin_core.h"

#include <string.h>

#include "input/doomkey.h"

uint8_t conin_map_key(uint16_t scan, uint16_t ch) {
    /* EFI scan codes (UEFI spec, "EFI Scan Codes for EFI_SIMPLE_TEXT_INPUT_PROTOCOL"). */
    static const uint8_t scan_map[0x18] = {
        [0x01] = DK_UP,  [0x02] = DK_DOWN, [0x03] = DK_RIGHT,  [0x04] = DK_LEFT, [0x05] = DK_HOME,
        [0x06] = DK_END, [0x07] = DK_INS,  [0x08] = DK_DEL,    [0x09] = DK_PGUP, [0x0a] = DK_PGDN,
        [0x15] = DK_F11, [0x16] = DK_F12,  [0x17] = DK_ESCAPE,
    };
    if (scan) {
        if (scan >= 0x0b && scan <= 0x14) {
            return (uint8_t)(DK_F1 + (scan - 0x0b)); /* F1..F10 are consecutive */
        }
        if (scan == 0x48) {
            return DK_PAUSE;
        }
        return scan < sizeof scan_map ? scan_map[scan] : 0;
    }
    switch (ch) {
    case 0x0d:
    case 0x0a:
        return DK_ENTER;
    case 0x08:
        return DK_BACKSPACE;
    case 0x09:
        return DK_TAB;
    case 0x1b:
        return DK_ESCAPE;
    case ' ':
        return DK_USE;
    case ',':
    case '<':
        return DK_STRAFE_L;
    case '.':
    case '>':
        return DK_STRAFE_R;
    default:
        break;
    }
    if (ch >= 1 && ch <= 26) {
        return (uint8_t)('a' + ch - 1); /* Ctrl+letter arrives as a control code */
    }
    if (ch >= 'A' && ch <= 'Z') {
        return (uint8_t)(ch + ('a' - 'A'));
    }
    if (ch > ' ' && ch < 0x7f) {
        return (uint8_t)ch;
    }
    return 0;
}

int conin_map_modifiers(uint32_t shift_state, uint8_t out[3]) {
    int n = 0;
    if (!(shift_state & CONIN_SHIFT_VALID)) {
        return 0;
    }
    if (shift_state & CONIN_CTRL_MASK) {
        out[n++] = DK_FIRE;
    }
    if (shift_state & CONIN_ALT_MASK) {
        out[n++] = DK_RALT;
    }
    if (shift_state & CONIN_SHIFT_MASK) {
        out[n++] = DK_RSHIFT;
    }
    return n;
}

void conin_pulse_init(conin_pulse *p, uint32_t initial_hold_us) {
    memset(p, 0, sizeof *p);
    p->initial_hold_us = initial_hold_us;
}

bool conin_pulse_stroke(conin_pulse *p, uint8_t key, uint64_t now_us) {
    uint64_t prev = p->last_us[key];
    uint32_t hold = p->initial_hold_us;
    if (prev && now_us - prev < CONIN_REPEAT_WINDOW_US) {
        uint64_t h = 2 * (now_us - prev); /* auto-repeat: hold across two intervals */
        hold = h < CONIN_HOLD_MIN_US   ? CONIN_HOLD_MIN_US
               : h > CONIN_HOLD_MAX_US ? CONIN_HOLD_MAX_US
                                       : (uint32_t)h;
    }
    p->last_us[key] = now_us ? now_us : 1;
    p->hold_us[key] = hold;
    bool was_held = p->held[key];
    p->held[key] = true;
    return !was_held;
}

int conin_pulse_expire(conin_pulse *p, uint64_t now_us) {
    for (int k = 0; k < 256; k++) {
        if (p->held[k] && now_us - p->last_us[k] > p->hold_us[k]) {
            p->held[k] = false;
            return k;
        }
    }
    return -1;
}
