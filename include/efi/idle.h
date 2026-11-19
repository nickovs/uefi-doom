/* Low-power waiting: the CPU halts in the firmware's idle loop (WaitForEvent)
 * between timer ticks instead of spinning. The firmware timer is sped up to
 * 1 kHz when the Timer architectural protocol allows it (restored at exit).
 * See docs/architecture.md#clock-and-idle. */
#ifndef UD_EFI_IDLE_H
#define UD_EFI_IDLE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Prepare idle waits: request a 1 ms timer tick and measure the wake-up
 * granularity actually achieved. Registers a cleanup. Call once, after
 * clock_init.
 */
void idle_init(void);

/**
 * Return once clock_us() >= deadline_us. Halts while more than one timer tick
 * remains, then spins the rest, so it neither oversleeps nor burns the CPU.
 */
void idle_until(uint64_t deadline_us);

/** Measured wake-up granularity in us; 0 if idle waits are unavailable (spin only). */
uint32_t idle_granularity_us(void);

/** Firmware timer period found at idle_init (us; 0 if unknown) and whether 1 ms was set. */
void idle_timer_info(uint32_t *original_us, bool *set_1ms);

#endif
