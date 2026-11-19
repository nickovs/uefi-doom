/* Doom key codes (the values of doomgeneric's doomkeys.h), duplicated so the
 * input modules do not depend on the engine. Ctrl, Space and the strafe keys
 * use doomgeneric's action codes, which its default bindings expect. */
#ifndef UD_INPUT_DOOMKEY_H
#define UD_INPUT_DOOMKEY_H

enum {
    DK_RIGHT = 0xae,
    DK_LEFT = 0xac,
    DK_UP = 0xad,
    DK_DOWN = 0xaf,
    DK_STRAFE_L = 0xa0,
    DK_STRAFE_R = 0xa1,
    DK_USE = 0xa2,
    DK_FIRE = 0xa3,
    DK_ESCAPE = 27,
    DK_ENTER = 13,
    DK_TAB = 9,
    DK_F1 = 0x80 + 0x3b, /* F1..F10 consecutive */
    DK_F11 = 0x80 + 0x57,
    DK_F12 = 0x80 + 0x58,
    DK_BACKSPACE = 0x7f,
    DK_PAUSE = 0xff,
    DK_RSHIFT = 0x80 + 0x36,
    DK_RALT = 0x80 + 0x38,
    DK_CAPSLOCK = 0x80 + 0x3a,
    DK_NUMLOCK = 0x80 + 0x45,
    DK_SCRLCK = 0x80 + 0x46,
    DK_PRTSCR = 0x80 + 0x59,
    DK_HOME = 0x80 + 0x47,
    DK_END = 0x80 + 0x4f,
    DK_PGUP = 0x80 + 0x49,
    DK_PGDN = 0x80 + 0x51,
    DK_INS = 0x80 + 0x52,
    DK_DEL = 0x80 + 0x53,
};

/** One key transition, in Doom key codes. */
typedef struct {
    unsigned char pressed; /* 1 = down, 0 = up */
    unsigned char key;
} key_event;

#endif
