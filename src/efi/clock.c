/* Clock source selection and calibration. See clock.h. */
#include "efi/clock.h"

#include "efi/acpi.h"
#include "efi/cpu.h"
#include "efi/efi.h"
#include "efi/log.h"

#define PM_TIMER_HZ 3579545u
#define CALIBRATE_MS 100
#define AGREE_PPM 1000 /* 0.1% */

/* HPET registers (offsets from the MMIO base). */
#define HPET_CAP 0x000 /* bits 63:32 = counter period in femtoseconds */
#define HPET_COUNTER 0x0f0

static clock_info info;
static uint64_t tsc_base;

static uint64_t cpuid_tsc_hz(void) {
    uint32_t r[4];
    cpuid(0, 0, r);
    if (r[0] < 0x15) {
        return 0;
    }
    cpuid(0x15, 0, r); /* eax = denominator, ebx = numerator, ecx = crystal Hz */
    if (r[0] == 0 || r[1] == 0 || r[2] == 0) {
        return 0;
    }
    return (uint64_t)r[2] * r[1] / r[0];
}

static bool cpu_has_invariant_tsc(void) {
    uint32_t r[4];
    cpuid(0x80000000, 0, r);
    if (r[0] < 0x80000007) {
        return false;
    }
    cpuid(0x80000007, 0, r);
    return (r[3] >> 8) & 1;
}

static uint32_t pm_read(void) {
    uint32_t v = inl(info.pm_port);
    return info.pm_32bit ? v : v & 0xffffff;
}

static uint64_t hpet_read(uint32_t off) {
    return *(volatile uint64_t *)(uintptr_t)(info.hpet_base + off);
}

uint64_t clock_measure_tsc_hz(clock_ref ref, uint32_t ms) {
    uint64_t t0, t1;
    switch (ref) {
    case CLOCK_REF_STALL:
        t0 = rdtsc();
        gBS->Stall((UINTN)ms * 1000);
        t1 = rdtsc();
        return (t1 - t0) * 1000 / ms;

    case CLOCK_REF_PMTIMER: {
        if (!info.pm_port) {
            return 0;
        }
        uint32_t mask = info.pm_32bit ? 0xffffffffu : 0xffffffu;
        uint64_t want = (uint64_t)PM_TIMER_HZ * ms / 1000, elapsed = 0;
        uint32_t start = pm_read(), prev = start;
        t0 = rdtsc();
        uint64_t guard = t0;
        while (elapsed < want) {
            uint32_t now = pm_read();
            elapsed += (now - prev) & mask;
            prev = now;
            if (now == start && rdtsc() - guard > (1ull << 33)) {
                return 0; /* ~3 s at 3 GHz with no movement: timer not counting */
            }
        }
        t1 = rdtsc();
        return (t1 - t0) * PM_TIMER_HZ / elapsed;
    }

    case CLOCK_REF_HPET: {
        if (!info.hpet_base) {
            return 0;
        }
        uint64_t period_fs = hpet_read(HPET_CAP) >> 32;
        if (period_fs == 0 || period_fs > 100000000) { /* spec: <= 100 ns */
            return 0;
        }
        uint64_t want = (uint64_t)ms * 1000000000000ull / period_fs;
        uint64_t c0 = hpet_read(HPET_COUNTER), c;
        t0 = rdtsc();
        while ((c = hpet_read(HPET_COUNTER)) - c0 < want) {
            if (c == c0 && rdtsc() - t0 > (1ull << 33)) {
                return 0; /* counter not running (disabled by firmware) */
            }
        }
        t1 = rdtsc();
        /* hz = ticks / seconds; seconds = (c - c0) * period_fs / 1e15 */
        return (uint64_t)((t1 - t0) * 1e15 / ((double)(c - c0) * (double)period_fs));
    }
    }
    return 0;
}

static uint64_t abs_ppm(uint64_t a, uint64_t b) {
    uint64_t d = a > b ? a - b : b - a;
    return d * 1000000 / b;
}

void clock_init(void) {
    acpi_fadt_info fadt;
    if (acpi_fadt(&fadt) && !fadt.hw_reduced) {
        info.pm_port = fadt.pm_tmr_port;
        info.pm_32bit = fadt.pm_tmr_32bit;
    }
    uint64_t hpet;
    if (acpi_hpet_base(&hpet)) {
        info.hpet_base = hpet;
    }
    info.invariant_tsc = cpu_has_invariant_tsc();
    info.cpuid_hz = cpuid_tsc_hz();

    /* Reference preference: PM timer, then HPET (Linux distrusts HPET on recent
     * Intel parts), then Stall. */
    static const clock_ref order[] = {CLOCK_REF_PMTIMER, CLOCK_REF_HPET, CLOCK_REF_STALL};
    for (unsigned i = 0; i < 3 && !info.measured_hz; i++) {
        info.measured_ref = order[i];
        info.measured_hz = clock_measure_tsc_hz(order[i], CALIBRATE_MS);
    }
    static const char *const ref_name[] = {"Stall", "PM timer", "HPET"};

    if (info.cpuid_hz && info.measured_hz &&
        abs_ppm(info.cpuid_hz, info.measured_hz) <= AGREE_PPM) {
        info.tsc_hz = info.cpuid_hz;
        info.source = "CPUID 0x15";
    } else if (info.measured_hz) {
        info.tsc_hz = info.measured_hz;
        info.source = ref_name[info.measured_ref];
    } else {
        info.tsc_hz = info.cpuid_hz ? info.cpuid_hz : 1000000000;
        info.source = info.cpuid_hz ? "CPUID 0x15 (unverified)" : "guess (1 GHz)";
    }
    tsc_base = rdtsc();

    log_msg(LOG_INFO, "clock: TSC %llu Hz from %s; invariant=%d cpuid=%llu measured=%llu (%s)",
            (unsigned long long)info.tsc_hz, info.source, info.invariant_tsc,
            (unsigned long long)info.cpuid_hz, (unsigned long long)info.measured_hz,
            ref_name[info.measured_ref]);
    if (info.cpuid_hz && info.measured_hz && info.tsc_hz != info.cpuid_hz) {
        log_msg(LOG_WARN, "clock: CPUID and %s disagree by %llu ppm", ref_name[info.measured_ref],
                (unsigned long long)abs_ppm(info.cpuid_hz, info.measured_hz));
    }
    /* shortcut: without an invariant TSC we still use it (out of scope: pre-2008
     * CPUs); upgrade to a PM-timer/HPET-driven clock if such a target appears. */
    if (!info.invariant_tsc) {
        log_msg(LOG_WARN, "clock: TSC is not invariant; timing may drift");
    }
}

const clock_info *clock_get_info(void) {
    return &info;
}

uint64_t clock_tsc_hz(void) {
    return info.tsc_hz;
}

uint64_t clock_us(void) {
    uint64_t d = rdtsc() - tsc_base;
    /* Split to avoid 64-bit overflow of d * 1e6. */
    return d / info.tsc_hz * 1000000 + d % info.tsc_hz * 1000000 / info.tsc_hz;
}

uint32_t clock_ms(void) {
    return (uint32_t)(clock_us() / 1000);
}

void clock_sleep_us(uint64_t us) {
    uint64_t end = clock_us() + us;
    while (clock_us() < end) {
        cpu_pause();
    }
}
