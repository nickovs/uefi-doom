/* Keymaps. See include/input/keymap_core.h. Ctrl is fire, Space is use and
 * ','/'.' strafe, matching doomgeneric's default bindings (as its X11 port does). */
#include "input/keymap_core.h"

#include "input/doomkey.h"

static const uint8_t set1[0x59] = {
    [0x01] = DK_ESCAPE,
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0a] = '9',
    [0x0b] = '0',
    [0x0c] = '-',
    [0x0d] = '=',
    [0x0e] = DK_BACKSPACE,
    [0x0f] = DK_TAB,
    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1a] = '[',
    [0x1b] = ']',
    [0x1c] = DK_ENTER,
    [0x1d] = DK_FIRE,
    [0x1e] = 'a',
    [0x1f] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',
    [0x2a] = DK_RSHIFT,
    [0x2b] = '\\',
    [0x2c] = 'z',
    [0x2d] = 'x',
    [0x2e] = 'c',
    [0x2f] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = DK_STRAFE_L,
    [0x34] = DK_STRAFE_R,
    [0x35] = '/',
    [0x36] = DK_RSHIFT,
    [0x37] = '*',
    [0x38] = DK_RALT,
    [0x39] = DK_USE,
    [0x3a] = DK_CAPSLOCK,
    [0x45] = DK_NUMLOCK,
    [0x46] = DK_SCRLCK,
    /* Keypad with Num Lock off (doomkeys.h KEYP_*): */
    [0x47] = DK_HOME,
    [0x48] = DK_UP,
    [0x49] = DK_PGUP,
    [0x4a] = '-',
    [0x4b] = DK_LEFT,
    [0x4c] = '5',
    [0x4d] = DK_RIGHT,
    [0x4e] = '+',
    [0x4f] = DK_END,
    [0x50] = DK_DOWN,
    [0x51] = DK_PGDN,
    [0x57] = DK_F11,
    [0x58] = DK_F12,
};

uint8_t keymap_set1(uint8_t code, bool e0) {
    if (!e0) {
        if (code >= 0x3b && code <= 0x44) {
            return (uint8_t)(DK_F1 + (code - 0x3b));
        }
        return code < sizeof set1 ? set1[code] : 0;
    }
    switch (code) {
    case 0x1c:
        return DK_ENTER; /* keypad Enter */
    case 0x1d:
        return DK_FIRE; /* right Ctrl */
    case 0x35:
        return '/';
    case 0x37:
        return DK_PRTSCR;
    case 0x38:
        return DK_RALT;
    case 0x47:
        return DK_HOME;
    case 0x48:
        return DK_UP;
    case 0x49:
        return DK_PGUP;
    case 0x4b:
        return DK_LEFT;
    case 0x4d:
        return DK_RIGHT;
    case 0x4f:
        return DK_END;
    case 0x50:
        return DK_DOWN;
    case 0x51:
        return DK_PGDN;
    case 0x52:
        return DK_INS;
    case 0x53:
        return DK_DEL;
    default:
        return 0; /* includes the fake shifts E0 2A / E0 36 */
    }
}

uint8_t keymap_hid(uint8_t u) {
    static const char digits[] = "1234567890";
    static const uint8_t misc[0x39 - 0x28] = {
        DK_ENTER,    DK_ESCAPE,   DK_BACKSPACE, DK_TAB,           DK_USE, '-',  '=',
        '[',         ']',         '\\',         0 /* non-US # */, ';',    '\'', '`',
        DK_STRAFE_L, DK_STRAFE_R, '/',
    };
    static const uint8_t nav[0x53 - 0x46] = {
        DK_PRTSCR, DK_SCRLCK, DK_PAUSE, DK_INS,  DK_HOME, DK_PGUP, DK_DEL,
        DK_END,    DK_PGDN,   DK_RIGHT, DK_LEFT, DK_DOWN, DK_UP,
    };
    static const uint8_t keypad[0x64 - 0x53] = {
        DK_NUMLOCK, '/', '*',      '-',     '+',   DK_ENTER, DK_END, DK_DOWN, DK_PGDN,
        DK_LEFT,    '5', DK_RIGHT, DK_HOME, DK_UP, DK_PGUP,  0,      0,
    };
    if (u >= 0x04 && u <= 0x1d) {
        return (uint8_t)('a' + u - 0x04);
    }
    if (u >= 0x1e && u <= 0x27) {
        return (uint8_t)digits[u - 0x1e];
    }
    if (u >= 0x28 && u < 0x39) {
        return misc[u - 0x28];
    }
    if (u == 0x39) {
        return DK_CAPSLOCK;
    }
    if (u >= 0x3a && u <= 0x43) {
        return (uint8_t)(DK_F1 + (u - 0x3a));
    }
    if (u == 0x44) {
        return DK_F11;
    }
    if (u == 0x45) {
        return DK_F12;
    }
    if (u >= 0x46 && u < 0x53) {
        return nav[u - 0x46];
    }
    if (u >= 0x53 && u < 0x64) {
        return keypad[u - 0x53];
    }
    return 0;
}
