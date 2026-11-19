/* Scancode and HID-usage keymaps to Doom keys (US layout; pure, host-tested).
 * See docs/architecture.md#input-input. */
#ifndef UD_INPUT_KEYMAP_CORE_H
#define UD_INPUT_KEYMAP_CORE_H

#include <stdbool.h>
#include <stdint.h>

/** Doom key for a set-1 make code (0x00-0x7f), `e0` if it had an E0 prefix; 0 if unmapped. */
uint8_t keymap_set1(uint8_t code, bool e0);

/** Doom key for a HID keyboard usage (page 7); 0 if unmapped. */
uint8_t keymap_hid(uint8_t usage);

#endif
