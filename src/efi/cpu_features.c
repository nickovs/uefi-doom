/* AVX enable/restore. See cpu_features.h. On real hardware 256-bit
 * stores double uncached framebuffer throughput (224 vs 109 MB/s). */
#include "efi/cpu_features.h"

#include <stdbool.h>

#include "efi/cpu.h"
#include "efi/efi_main.h"
#include "efi/log.h"

#define CR4_OSXSAVE (1ull << 18)
#define XCR0_X87_SSE_AVX 0x7ull

static uint64_t saved_cr4, saved_xcr0;
static avx_state state = AVX_UNAVAILABLE;
static bool probed;

void cpu_avx_restore(void) {
    if (state != AVX_ENABLED) {
        return;
    }
    __asm__ volatile("vzeroupper" ::: "memory");
    if (saved_cr4 & CR4_OSXSAVE) {
        xsetbv(0, saved_xcr0);
    } else {
        xsetbv(0, 0x3); /* x87 + SSE, then turn XSAVE support back off */
        write_cr4(read_cr4() & ~CR4_OSXSAVE);
    }
    state = AVX_UNAVAILABLE;
    probed = false;
    log_msg(LOG_INFO, "cpu: AVX state restored (CR4 0x%llx)", (unsigned long long)read_cr4());
}

static void restore_cleanup(void *ctx) {
    (void)ctx;
    cpu_avx_restore();
}

avx_state cpu_avx_enable(void) {
    if (probed) {
        return state;
    }
    probed = true;
    uint32_t r[4];
    cpuid(1, 0, r);
    bool xsave = (r[2] >> 26) & 1, avx = (r[2] >> 28) & 1;
    cpuid(0, 0, r);
    uint32_t max_leaf = r[0];
    if (!avx || !xsave || max_leaf < 0xd) {
        log_msg(LOG_INFO, "cpu: no AVX/XSAVE");
        return state = AVX_UNAVAILABLE;
    }
    cpuid(0xd, 0, r);
    if ((r[0] & XCR0_X87_SSE_AVX) != XCR0_X87_SSE_AVX) {
        log_msg(LOG_INFO, "cpu: XCR0 cannot hold YMM state (supported 0x%x)", r[0]);
        return state = AVX_UNAVAILABLE;
    }
    saved_cr4 = read_cr4();
    saved_xcr0 = (saved_cr4 & CR4_OSXSAVE) ? xgetbv(0) : 0;
    if ((saved_cr4 & CR4_OSXSAVE) && (saved_xcr0 & 6) == 6) {
        log_msg(LOG_INFO, "cpu: AVX already enabled by firmware");
        return state = AVX_ALREADY_ON;
    }
    efi_cleanup_push(restore_cleanup, NULL, "avx");
    write_cr4(saved_cr4 | CR4_OSXSAVE);
    xsetbv(0, saved_xcr0 | XCR0_X87_SSE_AVX);
    log_msg(LOG_INFO, "cpu: AVX state enabled for this session (CR4 0x%llx -> 0x%llx, XCR0 0x%llx)",
            (unsigned long long)saved_cr4, (unsigned long long)read_cr4(),
            (unsigned long long)xgetbv(0));
    return state = AVX_ENABLED;
}

void cpu_avx_saved(uint64_t *cr4, uint64_t *xcr0) {
    *cr4 = saved_cr4;
    *xcr0 = saved_xcr0;
}
