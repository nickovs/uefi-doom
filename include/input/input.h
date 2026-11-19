/* The game's input layer: brings up every keyboard and mouse source it can own
 * (i8042, USB HID boot devices) and falls back to ConIn, merging them into one
 * stream of Doom key events plus mouse motion. Ctrl+Alt+Backspace from any
 * source exits at once. See docs/architecture.md#input-input. */
#ifndef UD_INPUT_INPUT_H
#define UD_INPUT_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "input/doomkey.h"

typedef enum { INPUT_AUTO, INPUT_RAW, INPUT_CONIN } input_mode;

/* Mice: raw devices (USB boot mice, the PS/2 aux port) plus the firmware's
 * pointers (whatever it still drives, e.g. a touchpad); firmware only; none. */
typedef enum { MOUSE_AUTO, MOUSE_FIRMWARE, MOUSE_OFF } mouse_mode;

typedef struct {
    bool i8042;                                         /* internal keyboard read directly */
    int usb_kbds;                                       /* USB boot keyboards taken over */
    int usb_mice;                                       /* USB boot mice taken over */
    bool ps2_mouse;                                     /* PS/2 aux device streaming */
    int fw_pointers;                                    /* firmware pointers read */
    bool conin;                                         /* ConIn fallback active */
    uint32_t presses_i8042, presses_usb, presses_conin; /* key-downs accepted */
} input_info;

/** Bring up sources (registers cleanups that hand devices back). */
void input_init(input_mode mode, mouse_mode mice);

/** Read all sources into the event queue. */
void input_poll(void);

/** Pop the next key event; polls first if the queue is empty. */
bool input_next_key(key_event *ev);

/**
 * Mouse state since the last call: buttons (bit 0 left, 1 right, 2 middle) and
 * accumulated motion (dy positive = away from the user). False if unchanged.
 */
bool input_mouse(int *buttons, int *dx, int *dy);

/** Wait (low power) for any key press, up to timeout_ms; works before input_init too. */
bool input_wait_any_key(uint32_t timeout_ms);

const input_info *input_get_info(void);

#endif
