/* Optional CPU state the application may switch on for its own lifetime and
 * must switch off again before returning to firmware. */
#ifndef UD_EFI_CPU_FEATURES_H
#define UD_EFI_CPU_FEATURES_H

#include <stdint.h>

typedef enum {
    AVX_UNAVAILABLE, /* CPU lacks AVX/XSAVE, or XCR0 cannot hold YMM state */
    AVX_ALREADY_ON,  /* firmware had already enabled it: nothing to restore */
    AVX_ENABLED,     /* we enabled it; restored at exit */
} avx_state;

/**
 * Enable AVX (YMM) register state if the CPU supports it and firmware left it
 * off: sets CR4.OSXSAVE and XCR0 bits 1-2, registering a cleanup that restores
 * both. Firmware code is built without AVX and legacy SSE leaves the upper YMM
 * halves alone, so firmware callbacks are unaffected. Idempotent.
 */
avx_state cpu_avx_enable(void);

/** Undo cpu_avx_enable now (also runs at exit). */
void cpu_avx_restore(void);

/** CR4 and XCR0 as found before cpu_avx_enable changed them (for reports). */
void cpu_avx_saved(uint64_t *cr4, uint64_t *xcr0);

#endif
