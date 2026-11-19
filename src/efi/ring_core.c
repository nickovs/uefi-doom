#include "efi/ring_core.h"

void ring_init(log_ring *r, uint8_t *buf, size_t cap, size_t head_cap) {
    r->buf = buf;
    r->cap = cap;
    r->head_cap = head_cap;
    r->total = 0;
}

void ring_append(log_ring *r, const char *s, size_t n) {
    size_t circ = r->cap - r->head_cap;
    for (size_t i = 0; i < n; i++) {
        uint64_t pos = r->total++;
        if (pos < r->head_cap) {
            r->buf[pos] = (uint8_t)s[i];
        } else {
            r->buf[r->head_cap + (pos - r->head_cap) % circ] = (uint8_t)s[i];
        }
    }
}

size_t ring_snapshot(const log_ring *r, size_t budget, log_span out[3], uint64_t *dropped) {
    size_t count = 0;
    size_t circ = r->cap - r->head_cap;
    uint64_t head_len = r->total < r->head_cap ? r->total : r->head_cap;
    uint64_t tail_avail = r->total - head_len;
    if (tail_avail > circ) {
        tail_avail = circ;
    }

    size_t head_take = head_len < budget ? (size_t)head_len : budget;
    if (head_take) {
        out[count++] = (log_span){r->buf, head_take};
    }
    budget -= head_take;

    size_t tail_take = tail_avail < budget ? (size_t)tail_avail : budget;
    if (head_take < head_len) {
        tail_take = 0; /* budget exhausted inside the head */
    }
    if (tail_take) {
        /* Stream positions [total - tail_take, total) map into the circle. */
        uint64_t start = r->total - tail_take;
        size_t off = (size_t)((start - r->head_cap) % circ);
        size_t first = circ - off < tail_take ? circ - off : tail_take;
        out[count++] = (log_span){r->buf + r->head_cap + off, first};
        if (first < tail_take) {
            out[count++] = (log_span){r->buf + r->head_cap, tail_take - first};
        }
    }
    *dropped = r->total - head_take - tail_take;
    return count;
}
