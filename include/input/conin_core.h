/* ConIn fallback input (pure, host-tested): EFI keystroke -> doomkey mapping
 * and the pulse model that synthesises key releases from keystrokes.
 * See docs/architecture.md#input-input. */
#ifndef UD_INPUT_CONIN_CORE_H
#define UD_INPUT_CONIN_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* EFI_KEY_STATE.KeyShiftState bits (UEFI spec, Simple Text Input Ex). */
#define CONIN_SHIFT_VALID 0x80000000u
#define CONIN_SHIFT_MASK 0x00000003u /* right | left shift */
#define CONIN_CTRL_MASK 0x0000000cu
#define CONIN_ALT_MASK 0x00000030u

/** Doom key code for an EFI keystroke (scan code / UCS-2 char), or 0 if unmapped. */
uint8_t conin_map_key(uint16_t scan, uint16_t ch);

/**
 * Doom modifier keys held according to `shift_state` (only when
 * CONIN_SHIFT_VALID is set). Writes up to 3 keys to `out`; returns the count.
 */
int conin_map_modifiers(uint32_t shift_state, uint8_t out[3]);

#define CONIN_HOLD_MIN_US 40000u
#define CONIN_HOLD_MAX_US 150000u
#define CONIN_REPEAT_WINDOW_US 1000000u /* a re-press within this counts as auto-repeat */

typedef struct {
    uint32_t initial_hold_us;
    uint64_t last_us[256]; /* last keystroke per doomkey (0 = never) */
    uint32_t hold_us[256]; /* current release timeout per key */
    bool held[256];
} conin_pulse;

void conin_pulse_init(conin_pulse *p, uint32_t initial_hold_us);

/**
 * Record a keystroke for `key` at `now_us`. Returns true if the key was not
 * held, i.e. the caller should post a key-down.
 */
bool conin_pulse_stroke(conin_pulse *p, uint8_t key, uint64_t now_us);

/** Next held key whose hold has expired at `now_us` (now released), or -1. */
int conin_pulse_expire(conin_pulse *p, uint64_t now_us);

#endif
