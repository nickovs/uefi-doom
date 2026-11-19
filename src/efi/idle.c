/* Low-power waiting. See include/efi/idle.h. */
#include "efi/idle.h"

#include "efi/clock.h"
#include "efi/cpu.h"
#include "efi/efi.h"

#include <Protocol/Timer.h>

#include "efi/efi_main.h"
#include "efi/log.h"

#define WANT_PERIOD 10000u /* 100 ns units: 1 ms */
#define SAMPLES 8

EFI_GUID gEfiTimerArchProtocolGuid = EFI_TIMER_ARCH_PROTOCOL_GUID;

static EFI_EVENT timer;
static uint32_t granularity_us;
static EFI_TIMER_ARCH_PROTOCOL *arch;
static UINT64 saved_period;
static UINT64 found_period;
static bool set_ok;

static void idle_cleanup(void *ctx) {
    (void)ctx;
    if (arch) {
        EFI_STATUS st = arch->SetTimerPeriod(arch, saved_period);
        log_msg(EFI_ERROR(st) ? LOG_WARN : LOG_INFO, "idle: timer period restored to %llu us: %s",
                (unsigned long long)saved_period / 10, efi_status_str(st));
        arch = NULL;
    }
    if (timer) {
        gBS->CloseEvent(timer);
        timer = NULL;
    }
}

/* Sleep until the timer event fires `t100ns` after now; false on failure. */
static bool wait_timer(UINT64 t100ns) {
    UINTN idx;
    return !EFI_ERROR(gBS->SetTimer(timer, TimerRelative, t100ns)) &&
           !EFI_ERROR(gBS->WaitForEvent(1, &timer, &idx));
}

void idle_init(void) {
    EFI_STATUS st = gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &timer);
    if (EFI_ERROR(st)) {
        log_msg(LOG_WARN, "idle: CreateEvent failed (%s); spinning", efi_status_str(st));
        timer = NULL;
        return;
    }
    UINT64 period = 0;
    st = gBS->LocateProtocol(&gEfiTimerArchProtocolGuid, NULL, (void **)&arch);
    if (!EFI_ERROR(st) && arch && !EFI_ERROR(arch->GetTimerPeriod(arch, &period)) &&
        period > WANT_PERIOD) {
        saved_period = period;
        st = arch->SetTimerPeriod(arch, WANT_PERIOD);
        if (EFI_ERROR(st)) {
            log_msg(LOG_INFO, "idle: SetTimerPeriod(1 ms) refused: %s", efi_status_str(st));
            arch = NULL;
        }
    } else {
        arch = NULL; /* absent, or already fast enough: nothing to restore */
    }
    found_period = period;
    set_ok = arch != NULL;
    efi_cleanup_push(idle_cleanup, NULL, "idle");

    /* Granularity: the slowest of several shortest-possible waits, after one
     * wait to line up with a tick. */
    uint64_t worst = 0;
    bool ok = wait_timer(0);
    for (int i = 0; ok && i < SAMPLES; i++) {
        uint64_t t0 = clock_us();
        ok = wait_timer(1);
        uint64_t dt = clock_us() - t0;
        worst = dt > worst ? dt : worst;
    }
    granularity_us = ok && worst ? (uint32_t)worst : 0;
    log_msg(LOG_INFO, "idle: firmware timer %llu us -> %s; wake-up granularity %u us",
            (unsigned long long)period / 10, arch ? "1000 us" : "unchanged", granularity_us);
}

void idle_timer_info(uint32_t *original_us, bool *set_1ms) {
    *original_us = (uint32_t)(found_period / 10);
    *set_1ms = set_ok;
}

uint32_t idle_granularity_us(void) {
    return granularity_us;
}

void idle_until(uint64_t deadline_us) {
    for (;;) {
        uint64_t now = clock_us();
        if (now >= deadline_us) {
            return;
        }
        uint64_t rem = deadline_us - now;
        /* Halt one tick at a time and re-check our own clock: firmware time
         * can run slow (QEMU delivers "1 ms" ticks every ~1.3 ms), so a single
         * long timer would oversleep. Spin the last tick. */
        if (granularity_us && rem > (uint64_t)granularity_us + 200) {
            if (!wait_timer(1)) {
                granularity_us = 0;
            }
        } else {
            cpu_pause();
        }
    }
}
