/* HWPROBE speaker section: is the 8254 PIT channel 2 counting, and (only when
 * interactive) can the user hear tones through port 0x61? */
#include "efi/acpi.h"
#include "efi/clock.h"
#include "efi/cpu.h"
#include "efi/efi_main.h"
#include "efi/log.h"
#include "hwprobe.h"

#define PIT_HZ 1193182u
#define PIT_CH2 0x42
#define PIT_CMD 0x43
#define SPEAKER_PORT 0x61 /* bit 0: PIT ch2 gate, bit 1: speaker data enable */

static uint8_t saved61;
static bool touched;

static void speaker_restore(void *ctx) {
    (void)ctx;
    if (touched) {
        outb(SPEAKER_PORT, (uint8_t)((inb(SPEAKER_PORT) & ~3u) | (saved61 & 3u)));
        touched = false;
    }
}

static void pit_ch2_square(uint32_t hz) {
    uint32_t div = PIT_HZ / hz;
    outb(PIT_CMD, 0xb6); /* channel 2, lo/hi byte, mode 3 (square wave), binary */
    outb(PIT_CH2, (uint8_t)div);
    outb(PIT_CH2, (uint8_t)(div >> 8));
}

static uint16_t pit_ch2_read(void) {
    outb(PIT_CMD, 0x80); /* latch channel 2 */
    uint8_t lo = inb(PIT_CH2), hi = inb(PIT_CH2);
    return (uint16_t)(hi << 8 | lo);
}

void probe_speaker(void) {
    log_msg(LOG_INFO, "== speaker");
    rep_obj("speaker");
    int spk = acpi_eisaid_count("PNP0800"), pit = acpi_eisaid_count("PNP0100");
    rep_int("acpi_pnp0800", spk);
    rep_int("acpi_pnp0100", pit);

    saved61 = inb(SPEAKER_PORT);
    touched = true;
    efi_cleanup_push(speaker_restore, NULL, "speaker");

    /* Gate on, speaker data off: silent, but channel 2 should count. */
    outb(SPEAKER_PORT, (uint8_t)((saved61 & ~2u) | 1u));
    pit_ch2_square(1000);
    uint16_t a = pit_ch2_read();
    clock_sleep_us(200);
    uint16_t b = pit_ch2_read();
    bool counting = a != b;
    log_msg(LOG_INFO, "PIT ch2: %s (0x%04x -> 0x%04x); ACPI PNP0800=%d PNP0100=%d",
            counting ? "counting" : "NOT counting (clock-gated or absent)", a, b, spk, pit);
    rep_bool("pit_ch2_counting", counting);

    if (counting &&
        hw_wait_enter("PC speaker. After ENTER, three rising tones play (about 1.5 s).", 120000)) {
        static const uint32_t tones[] = {440, 880, 1760};
        log_msg(LOG_INFO, "Playing three tones through the PC speaker...");
        for (unsigned i = 0; i < 3; i++) {
            pit_ch2_square(tones[i]);
            outb(SPEAKER_PORT, (uint8_t)(inb(SPEAKER_PORT) | 3u));
            clock_sleep_us(300000);
            outb(SPEAKER_PORT, (uint8_t)(inb(SPEAKER_PORT) & ~2u));
            clock_sleep_us(150000);
        }
        rep_bool("heard", hw_ask("Did you hear three tones?", 60000));
    }
    speaker_restore(NULL);
    rep_end();
}
