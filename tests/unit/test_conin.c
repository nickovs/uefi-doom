#include "check.h"
#include "input/conin_core.h"

#define MS 1000u

int main(void) {
    /* Keymap: scan codes, characters, case folding, Doom's special keys. */
    CHECK_EQ(conin_map_key(0x01, 0), 0xad);        /* up */
    CHECK_EQ(conin_map_key(0x17, 0), 27);          /* Esc */
    CHECK_EQ(conin_map_key(0x0b, 0), 0x80 + 0x3b); /* F1 */
    CHECK_EQ(conin_map_key(0x14, 0), 0x80 + 0x44); /* F10 */
    CHECK_EQ(conin_map_key(0x16, 0), 0x80 + 0x58); /* F12 */
    CHECK_EQ(conin_map_key(0x48, 0), 0xff);        /* Pause */
    CHECK_EQ(conin_map_key(0x7f, 0), 0);           /* unknown scan code */
    CHECK_EQ(conin_map_key(0, 0x0d), 13);
    CHECK_EQ(conin_map_key(0, 'Y'), 'y');
    CHECK_EQ(conin_map_key(0, 'y'), 'y');
    CHECK_EQ(conin_map_key(0, ' '), 0xa2); /* use */
    CHECK_EQ(conin_map_key(0, ','), 0xa0); /* strafe left */
    CHECK_EQ(conin_map_key(0, 0x19), 'y'); /* Ctrl+Y */
    CHECK_EQ(conin_map_key(0, 0x08), 0x7f);
    CHECK_EQ(conin_map_key(0, 0x00e9), 0); /* non-ASCII */

    uint8_t m[3];
    CHECK_EQ(conin_map_modifiers(0x0000000c, m), 0); /* not valid */
    CHECK_EQ(conin_map_modifiers(CONIN_SHIFT_VALID | 0x4 | 0x20 | 0x1, m), 3);
    CHECK_EQ(m[0], 0xa3); /* Ctrl -> fire */
    CHECK_EQ(m[1], 0x80 + 0x38);
    CHECK_EQ(m[2], 0x80 + 0x36);

    /* Pulse: a single stroke holds for initial_hold, then releases once. */
    static conin_pulse p;
    conin_pulse_init(&p, 100 * MS);
    uint64_t t = 5000 * MS;
    CHECK(conin_pulse_stroke(&p, 'a', t));
    CHECK_EQ(conin_pulse_expire(&p, t + 100 * MS), -1);
    CHECK_EQ(conin_pulse_expire(&p, t + 101 * MS), 'a');
    CHECK_EQ(conin_pulse_expire(&p, t + 200 * MS), -1);

    /* Auto-repeat like a real laptop's: first repeat after 584 ms (a new
     * press after the initial release), then every 95 ms: held continuously,
     * with the hold capped at 150 ms. */
    t = 10000 * MS;
    CHECK(conin_pulse_stroke(&p, 'w', t));
    CHECK_EQ(conin_pulse_expire(&p, t + 150 * MS), 'w');
    t += 584 * MS;
    CHECK(conin_pulse_stroke(&p, 'w', t)); /* re-press: key-down again */
    CHECK_EQ(p.hold_us['w'], CONIN_HOLD_MAX_US);
    for (int i = 0; i < 10; i++) {
        CHECK_EQ(conin_pulse_expire(&p, t + 94 * MS), -1);
        t += 95 * MS;
        CHECK(!conin_pulse_stroke(&p, 'w', t)); /* still held: no new key-down */
    }
    CHECK_EQ(p.hold_us['w'], CONIN_HOLD_MAX_US); /* 2 x 95 ms capped */
    CHECK_EQ(conin_pulse_expire(&p, t + 150 * MS), -1);
    CHECK_EQ(conin_pulse_expire(&p, t + 151 * MS), 'w');

    /* Fast repeat (30 ms): hold is twice the interval, floored at 40 ms. */
    t = 20000 * MS;
    conin_pulse_stroke(&p, 'x', t);
    conin_pulse_stroke(&p, 'x', t + 30 * MS);
    CHECK_EQ(p.hold_us['x'], 60 * MS);
    conin_pulse_stroke(&p, 'x', t + 40 * MS);
    CHECK_EQ(p.hold_us['x'], CONIN_HOLD_MIN_US);

    /* A press long after the last one is not a repeat. */
    conin_pulse_expire(&p, t + 1000 * MS);
    conin_pulse_stroke(&p, 'x', t + 3000 * MS);
    CHECK_EQ(p.hold_us['x'], 100 * MS);
    return CHECK_DONE();
}
