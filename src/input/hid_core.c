/* HID boot reports. See include/input/hid_core.h. */
#include "input/hid_core.h"

#include <string.h>

#include "input/keymap_core.h"

static void set(uint8_t *bm, uint8_t k) {
    bm[k >> 3] |= (uint8_t)(1u << (k & 7));
}

static int diff(hid_kbd *k, const uint8_t now[32], key_event *out) {
    int n = 0;
    for (int pass = 0; pass < 2; pass++) { /* ups first, then downs */
        for (int key = 1; key < 256; key++) {
            int was = k->held[key >> 3] >> (key & 7) & 1, is = now[key >> 3] >> (key & 7) & 1;
            if (was != is && is == pass && n < HID_KBD_MAX_EVENTS) {
                out[n++] = (key_event){(unsigned char)is, (unsigned char)key};
            }
        }
    }
    memcpy(k->held, now, 32);
    return n;
}

int hid_kbd_report(hid_kbd *k, const uint8_t *rep, size_t len, key_event *out) {
    if (len < 3) {
        return 0;
    }
    size_t slots = len < 8 ? len : 8;
    for (size_t i = 2; i < slots; i++) {
        if (rep[i] == 0x01) {
            return 0; /* ErrorRollOver: too many keys; keep the previous state */
        }
    }
    uint8_t now[32] = {0};
    static const uint8_t mod_keys[3] = {DK_FIRE, DK_RSHIFT, DK_RALT}; /* Ctrl, Shift, Alt */
    for (int bit = 0; bit < 8; bit++) {
        if ((rep[0] >> bit & 1) && (bit & 3) != 3) { /* bits 3/7 are GUI: unused */
            set(now, mod_keys[bit & 3]);
        }
    }
    for (size_t i = 2; i < slots; i++) {
        uint8_t key = keymap_hid(rep[i]);
        if (key) {
            set(now, key);
        }
    }
    return diff(k, now, out);
}

int hid_kbd_release_all(hid_kbd *k, key_event *out) {
    static const uint8_t none[32];
    return diff(k, none, out);
}

int hid_mouse_report(const uint8_t *rep, size_t len, int *buttons, int *dx, int *dy) {
    if (len < 3) {
        return 0;
    }
    *buttons = rep[0] & 7;
    *dx = (int8_t)rep[1];
    *dy = (int8_t)rep[2];
    return 1;
}
