/* Monotonic clock: invariant TSC whose frequency comes from CPUID leaf 0x15,
 * cross-checked against (or measured with) the ACPI PM timer, HPET, or
 * gBS->Stall. See docs/architecture.md#clock-and-idle. */
#ifndef UD_EFI_CLOCK_H
#define UD_EFI_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { CLOCK_REF_STALL, CLOCK_REF_PMTIMER, CLOCK_REF_HPET } clock_ref;

typedef struct {
    bool invariant_tsc;
    uint64_t cpuid_hz;    /* from CPUID 0x15, 0 if not reported */
    uint64_t measured_hz; /* against `measured_ref`, 0 if not measured */
    clock_ref measured_ref;
    uint64_t tsc_hz;    /* value in use */
    const char *source; /* human-readable description of tsc_hz */
    uint16_t pm_port;   /* 0 if no PM timer */
    bool pm_32bit;
    uint64_t hpet_base; /* 0 if no HPET */
} clock_info;

/** Choose and calibrate the clock; call once at startup. */
void clock_init(void);

/** Details of the clock choice, for logs and HWPROBE. */
const clock_info *clock_get_info(void);

/** Microseconds since clock_init. */
uint64_t clock_us(void);

/** Milliseconds since clock_init (wraps after ~49 days). */
uint32_t clock_ms(void);

/** Busy-wait for at least `us` microseconds. */
void clock_sleep_us(uint64_t us);

/** TSC ticks per second in use. */
uint64_t clock_tsc_hz(void);

/**
 * Measure the TSC frequency over `ms` milliseconds against `ref`.
 * Returns 0 if `ref` is unavailable or does not count.
 */
uint64_t clock_measure_tsc_hz(clock_ref ref, uint32_t ms);

#endif
