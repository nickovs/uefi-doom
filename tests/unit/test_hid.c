#include "check.h"
#include "input/doomkey.h"
#include "input/hid_core.h"
#include "input/keymap_core.h"

int main(void) {
    hid_kbd k = {0};
    key_event ev[HID_KBD_MAX_EVENTS];

    /* Left Ctrl + Up (usage 0x52). */
    const uint8_t r1[8] = {0x01, 0, 0x52};
    CHECK_EQ(hid_kbd_report(&k, r1, 8, ev), 2);
    CHECK(ev[0].pressed && ev[1].pressed);
    CHECK((ev[0].key == DK_UP && ev[1].key == DK_FIRE) ||
          (ev[0].key == DK_FIRE && ev[1].key == DK_UP));

    /* Same report again: nothing. Add Right Ctrl: still one Doom key, nothing. */
    CHECK_EQ(hid_kbd_report(&k, r1, 8, ev), 0);
    const uint8_t r2[8] = {0x11, 0, 0x52};
    CHECK_EQ(hid_kbd_report(&k, r2, 8, ev), 0);

    /* Release Up, press 'a' (usage 4) in the same report: up comes first. */
    const uint8_t r3[8] = {0x11, 0, 0x04};
    CHECK_EQ(hid_kbd_report(&k, r3, 8, ev), 2);
    CHECK(!ev[0].pressed && ev[0].key == DK_UP);
    CHECK(ev[1].pressed && ev[1].key == 'a');

    /* Rollover error keeps the state; a short report is ignored. */
    const uint8_t roll[8] = {0x11, 0, 1, 1, 1, 1, 1, 1};
    CHECK_EQ(hid_kbd_report(&k, roll, 8, ev), 0);
    CHECK_EQ(hid_kbd_report(&k, r3, 2, ev), 0);

    /* GUI keys are ignored; device loss releases everything held. */
    const uint8_t gui[8] = {0x88, 0, 0x04};
    CHECK_EQ(hid_kbd_report(&k, gui, 8, ev), 1); /* only Ctrl released */
    CHECK(!ev[0].pressed && ev[0].key == DK_FIRE);
    CHECK_EQ(hid_kbd_release_all(&k, ev), 1);
    CHECK(!ev[0].pressed && ev[0].key == 'a');
    CHECK_EQ(hid_kbd_release_all(&k, ev), 0);

    /* Six keys at once, then none. */
    const uint8_t six[8] = {0, 0, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    CHECK_EQ(hid_kbd_report(&k, six, 8, ev), 6);
    const uint8_t none[8] = {0};
    CHECK_EQ(hid_kbd_report(&k, none, 8, ev), 6);

    /* Keymap spot checks. */
    CHECK_EQ(keymap_hid(0x1d), 'z');
    CHECK_EQ(keymap_hid(0x27), '0');
    CHECK_EQ(keymap_hid(0x2c), DK_USE);
    CHECK_EQ(keymap_hid(0x2a), DK_BACKSPACE);
    CHECK_EQ(keymap_hid(0x3a), DK_F1);
    CHECK_EQ(keymap_hid(0x45), DK_F12);
    CHECK_EQ(keymap_hid(0x4f), DK_RIGHT);
    CHECK_EQ(keymap_hid(0x52), DK_UP);
    CHECK_EQ(keymap_hid(0x60), DK_UP); /* keypad 8 */
    CHECK_EQ(keymap_hid(0x62), 0);     /* keypad 0 */
    CHECK_EQ(keymap_hid(0xe0), 0);     /* modifiers come from byte 0 */

    /* Mouse: signed deltas. */
    int b, dx, dy;
    const uint8_t m[4] = {0x05, 0xfe, 0x03, 0};
    CHECK(hid_mouse_report(m, 4, &b, &dx, &dy));
    CHECK_EQ(b, 5);
    CHECK_EQ(dx, -2);
    CHECK_EQ(dy, 3);
    CHECK(!hid_mouse_report(m, 2, &b, &dx, &dy));
    return CHECK_DONE();
}
