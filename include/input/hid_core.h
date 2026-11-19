/* USB HID boot-protocol keyboard and mouse reports (pure, host-tested). */
#ifndef UD_INPUT_HID_CORE_H
#define UD_INPUT_HID_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "input/doomkey.h"

#define HID_KBD_MAX_EVENTS 32 /* enough for any one report's changes */

typedef struct {
    uint8_t held[32]; /* bitmap of Doom keys down */
} hid_kbd;

/**
 * Diff a boot keyboard report ([modifiers, reserved, 6 usages]) against the
 * previous one. Writes key-ups then key-downs to `out` (room for
 * HID_KBD_MAX_EVENTS); returns the count. Rollover-error and short reports
 * are ignored (state kept).
 */
int hid_kbd_report(hid_kbd *k, const uint8_t *rep, size_t len, key_event *out);

/** Release every key still held (device lost). Returns the count. */
int hid_kbd_release_all(hid_kbd *k, key_event *out);

/** Parse a boot mouse report ([buttons, dx, dy, ...]); false if too short. */
int hid_mouse_report(const uint8_t *rep, size_t len, int *buttons, int *dx, int *dy);

#endif
