/* HWPROBE clock section: sources, calibration agreement, Stall accuracy,
 * timer-event granularity and the cost of slow firmware calls. */

#include "efi/clock.h"
#include "efi/cpu.h"
#include "efi/efi.h"
#include "efi/idle.h"
#include "efi/log.h"
#include "hwprobe.h"

#define TICKS_MAX 256

static volatile uint32_t nticks;
static uint64_t ticks[TICKS_MAX];

static void EFIAPI on_tick(EFI_EVENT ev, void *ctx) {
    (void)ev;
    (void)ctx;
    if (nticks < TICKS_MAX) {
        ticks[nticks] = clock_us();
    }
    nticks++;
}

static int64_t ppm(uint64_t a, uint64_t b) {
    return b ? ((int64_t)a - (int64_t)b) * 1000000 / (int64_t)b : 0;
}

/* Median cost in ns of `fn` over n calls. */
static uint64_t cost_ns(void (*fn)(void), int n) {
    uint64_t v[64];
    n = n > 64 ? 64 : n;
    for (int i = 0; i < n; i++) {
        uint64_t t0 = rdtsc();
        fn();
        v[i] = (rdtsc() - t0) * 1000000000 / clock_tsc_hz();
    }
    return hw_median(v, n);
}

static void read_pm(void) {
    (void)inl(clock_get_info()->pm_port);
}

static void read_hpet(void) {
    (void)*(volatile uint64_t *)(uintptr_t)(clock_get_info()->hpet_base + 0xf0);
}

static void get_time(void) {
    EFI_TIME t;
    gRT->GetTime(&t, NULL);
}

static void conout_line(void) {
    gST->ConOut->OutputString(gST->ConOut,
                              L"hwprobe: ConOut timing line .............................."
                              L"...................\r\n");
}

void probe_clock(void) {
    const clock_info *ci = clock_get_info();
    log_msg(LOG_INFO, "== clock");
    rep_obj("clock");
    rep_bool("invariant_tsc", ci->invariant_tsc);
    rep_int("tsc_hz", (int64_t)ci->tsc_hz);
    rep_str("tsc_source", ci->source);
    rep_int("cpuid_hz", (int64_t)ci->cpuid_hz);
    rep_hex("pm_timer_port", ci->pm_port);
    rep_hex("hpet_base", ci->hpet_base);

    static const char *const names[] = {"stall", "pm_timer", "hpet"};
    rep_obj("measured_hz");
    for (int r = CLOCK_REF_STALL; r <= CLOCK_REF_HPET; r++) {
        uint64_t hz = clock_measure_tsc_hz((clock_ref)r, 100);
        log_msg(LOG_INFO, "TSC vs %-8s: %llu Hz (%lld ppm vs CPUID)", names[r],
                (unsigned long long)hz, (long long)ppm(hz, ci->cpuid_hz));
        rep_int(names[r], (int64_t)hz);
    }
    rep_end();

    /* Stall(1000): how long does 1 ms really take? */
    uint64_t st[100];
    for (int i = 0; i < 100; i++) {
        uint64_t t0 = clock_us();
        gBS->Stall(1000);
        st[i] = clock_us() - t0;
    }
    uint64_t smed = hw_median(st, 100);
    log_msg(LOG_INFO, "Stall(1000): min %llu med %llu max %llu us", (unsigned long long)st[0],
            (unsigned long long)smed, (unsigned long long)st[99]);
    rep_obj("stall_1ms_us");
    rep_int("min", (int64_t)st[0]);
    rep_int("median", (int64_t)smed);
    rep_int("max", (int64_t)st[99]);
    rep_end();

    /* Timer-event granularity: ask for 1 ms periodic, see what we get. */
    EFI_EVENT ev;
    nticks = 0;
    if (!EFI_ERROR(
            gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, on_tick, NULL, &ev))) {
        gBS->SetTimer(ev, TimerPeriodic, 10000); /* 1 ms in 100 ns units */
        uint64_t end = clock_us() + 300000;
        while (clock_us() < end) {
            cpu_pause();
        }
        gBS->SetTimer(ev, TimerCancel, 0);
        gBS->CloseEvent(ev);
        uint32_t n = nticks < TICKS_MAX ? nticks : TICKS_MAX;
        uint64_t gaps[TICKS_MAX];
        for (uint32_t i = 1; i < n; i++) {
            gaps[i - 1] = ticks[i] - ticks[i - 1];
        }
        uint64_t gmed = n > 1 ? hw_median(gaps, (int)n - 1) : 0;
        log_msg(LOG_INFO, "1 ms periodic timer event: %u callbacks in 300 ms, median gap %llu us",
                nticks, (unsigned long long)gmed);
        rep_obj("timer_event_1ms");
        rep_int("callbacks_300ms", nticks);
        rep_int("median_gap_us", (int64_t)gmed);
        if (n > 1) {
            rep_int("min_gap_us", (int64_t)gaps[0]);
            rep_int("max_gap_us", (int64_t)gaps[n - 2]);
        }
        rep_end();
    }

    /* Costs of individual calls. */
    rep_obj("call_cost_ns");
    if (ci->pm_port) {
        uint64_t c = cost_ns(read_pm, 64);
        log_msg(LOG_INFO, "PM timer read: %llu ns", (unsigned long long)c);
        rep_int("pm_timer_read", (int64_t)c);
    }
    if (ci->hpet_base) {
        uint64_t c = cost_ns(read_hpet, 64);
        log_msg(LOG_INFO, "HPET read: %llu ns", (unsigned long long)c);
        rep_int("hpet_read", (int64_t)c);
    }
    uint64_t gt = cost_ns(get_time, 16);
    EFI_TIME t;
    EFI_TIME_CAPABILITIES caps = {0};
    gRT->GetTime(&t, &caps);
    log_msg(LOG_INFO, "GetTime: %llu ns per call; resolution %u Hz, accuracy %u ppm",
            (unsigned long long)gt, caps.Resolution, caps.Accuracy / 1000000);
    rep_int("get_time", (int64_t)gt);
    rep_int("get_time_resolution_hz", caps.Resolution);
    uint64_t co = cost_ns(conout_line, 8);
    log_msg(LOG_INFO, "ConOut 80-char line: %llu us", (unsigned long long)(co / 1000));
    rep_int("conout_line", (int64_t)co);
    rep_end();

    /* What DOOM.EFI's low-power idle will get on this machine. */
    idle_init();
    uint32_t orig_us, g = idle_granularity_us();
    bool set;
    idle_timer_info(&orig_us, &set);
    uint32_t r[4];
    cpuid(7, 0, r);
    bool waitpkg = (r[2] >> 5) & 1;
    uint64_t late[20], late_max = 0;
    for (int i = 0; i < 20; i++) {
        uint64_t want = clock_us() + 20000;
        idle_until(want);
        late[i] = clock_us() - want;
        late_max = late[i] > late_max ? late[i] : late_max;
    }
    uint64_t late_med = hw_median(late, 20);
    log_msg(LOG_INFO,
            "idle: timer %u us%s; wake-up granularity %u us; 20 ms waits end %llu us late "
            "(median), %llu us (max); WAITPKG %s",
            orig_us, set ? " -> 1000 us" : " (unchanged)", g, (unsigned long long)late_med,
            (unsigned long long)late_max, waitpkg ? "yes" : "no");
    rep_obj("idle");
    rep_int("timer_period_us", orig_us);
    rep_bool("set_1ms", set);
    rep_int("granularity_us", g);
    rep_int("wait_20ms_late_median_us", (int64_t)late_med);
    rep_int("wait_20ms_late_max_us", (int64_t)late_max);
    rep_bool("waitpkg", waitpkg);
    rep_end();
    rep_end();
}
