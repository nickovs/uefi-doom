/* Log storage: keeps the first `head_cap` bytes ever written (initialisation and
 * hardware detection) plus the most recent bytes in a circular area.
 * Pure code: no UEFI dependencies, unit-tested on the host. */
#ifndef UD_EFI_RING_CORE_H
#define UD_EFI_RING_CORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t cap;      /* total bytes in buf */
    size_t head_cap; /* bytes reserved for the head (first bytes of the stream) */
    uint64_t total;  /* bytes ever appended */
} log_ring;

typedef struct {
    const uint8_t *p;
    size_t n;
} log_span;

/** Initialise `r` over `buf`. Requires head_cap < cap. */
void ring_init(log_ring *r, uint8_t *buf, size_t cap, size_t head_cap);

/** Append `n` bytes; the oldest non-head bytes are overwritten when full. */
void ring_append(log_ring *r, const char *s, size_t n);

/**
 * Describe at most `budget` bytes of the log as up to three spans, in stream
 * order: the head first, then as much of the most recent data as fits.
 * Returns the number of spans written to `out`. `*dropped` receives the number
 * of stream bytes not covered by the spans.
 */
size_t ring_snapshot(const log_ring *r, size_t budget, log_span out[3], uint64_t *dropped);

#endif
