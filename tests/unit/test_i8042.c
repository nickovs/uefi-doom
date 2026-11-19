#include "check.h"
#include "input/doomkey.h"
#include "input/i8042_core.h"
#include "input/keymap_core.h"

static key_event ev[64];
static int nev;

static void feed(i8042_dec *d, const uint8_t *b, int n) {
    nev = 0;
    for (int i = 0; i < n; i++) {
        nev += i8042_dec_byte(d, b[i], ev + nev);
    }
}

int main(void) {
    i8042_dec d = {0};

    /* Captured from a real laptop: Left Ctrl held (typematic), then Up. */
    const uint8_t cap[] = {0x1d, 0x1d, 0x1d, 0xe0, 0x48, 0xe0, 0xc8, 0x9d};
    feed(&d, cap, sizeof cap);
    CHECK_EQ(nev, 6);
    CHECK(ev[0].pressed && ev[0].key == DK_FIRE);
    CHECK(ev[1].pressed && ev[1].key == DK_FIRE); /* repeats pass through; input.c dedupes */
    CHECK(ev[3].pressed && ev[3].key == DK_UP);
    CHECK(!ev[4].pressed && ev[4].key == DK_UP);
    CHECK(!ev[5].pressed && ev[5].key == DK_FIRE);

    /* Keypad 8 without E0 is also Up (Num Lock off); E0 1D is right Ctrl. */
    const uint8_t kp[] = {0x48, 0xc8, 0xe0, 0x1d, 0xe0, 0x9d};
    feed(&d, kp, sizeof kp);
    CHECK_EQ(nev, 4);
    CHECK(ev[0].key == DK_UP && ev[2].key == DK_FIRE && !ev[3].pressed);

    /* Fake shifts around extended keys are dropped; ACK/resend/overrun ignored. */
    const uint8_t fake[] = {0xe0, 0x2a, 0xe0, 0x47, 0xe0, 0xc7, 0xe0, 0xaa, 0xfa, 0xfe, 0x00, 0xff};
    feed(&d, fake, sizeof fake);
    CHECK_EQ(nev, 2);
    CHECK(ev[0].key == DK_HOME && ev[0].pressed && !ev[1].pressed);

    /* Pause: six bytes, one tap. */
    const uint8_t pause[] = {0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5};
    feed(&d, pause, sizeof pause);
    CHECK_EQ(nev, 2);
    CHECK(ev[0].key == DK_PAUSE && ev[0].pressed && !ev[1].pressed);
    feed(&d, (const uint8_t[]){0x1e, 0x9e}, 2); /* decoder back to normal: 'a' */
    CHECK(nev == 2 && ev[0].key == 'a');

    /* Keymap spot checks. */
    CHECK_EQ(keymap_set1(0x39, 0), DK_USE);
    CHECK_EQ(keymap_set1(0x0e, 0), DK_BACKSPACE);
    CHECK_EQ(keymap_set1(0x38, 0), DK_RALT);
    CHECK_EQ(keymap_set1(0x2a, 0), DK_RSHIFT);
    CHECK_EQ(keymap_set1(0x3b, 0), DK_F1);
    CHECK_EQ(keymap_set1(0x44, 0), DK_F1 + 9);
    CHECK_EQ(keymap_set1(0x58, 0), DK_F12);
    CHECK_EQ(keymap_set1(0x15, 0), 'y');
    CHECK_EQ(keymap_set1(0x33, 0), DK_STRAFE_L);
    CHECK_EQ(keymap_set1(0x5b, 1), 0); /* Windows key */
    CHECK_EQ(keymap_set1(0x7f, 0), 0);
    return CHECK_DONE();
}
