#include "check.h"
#include "efi/ring_core.h"

/* Concatenate the snapshot spans into out; returns length. */
static size_t flatten(const log_ring *r, size_t budget, char *out, uint64_t *dropped) {
    log_span s[3];
    size_t n = ring_snapshot(r, budget, s, dropped), len = 0;
    for (size_t i = 0; i < n; i++) {
        memcpy(out + len, s[i].p, s[i].n);
        len += s[i].n;
    }
    out[len] = 0;
    return len;
}

int main(void) {
    uint8_t buf[10];
    log_ring r;
    char out[64];
    uint64_t dropped;

    /* Empty ring. */
    ring_init(&r, buf, sizeof buf, 4);
    CHECK_EQ(flatten(&r, 100, out, &dropped), 0);
    CHECK_EQ(dropped, 0);

    /* Fits entirely. */
    ring_append(&r, "abcdef", 6);
    flatten(&r, 100, out, &dropped);
    CHECK_STR(out, "abcdef");
    CHECK_EQ(dropped, 0);

    /* Overflow: head "abcd" kept, circle (6 bytes) holds most recent data. */
    ring_append(&r, "ghijklmn", 8); /* stream: abcdefghijklmn (14) */
    flatten(&r, 100, out, &dropped);
    CHECK_STR(out, "abcdijklmn");
    CHECK_EQ(dropped, 4); /* efgh */

    /* Budget smaller than head + tail: head first, then newest tail bytes. */
    flatten(&r, 7, out, &dropped);
    CHECK_STR(out, "abcdlmn");
    CHECK_EQ(dropped, 7);

    /* Budget inside the head. */
    flatten(&r, 3, out, &dropped);
    CHECK_STR(out, "abc");
    CHECK_EQ(dropped, 11);

    /* Wrap-around across many cycles stays consistent. */
    ring_init(&r, buf, sizeof buf, 2);
    for (int i = 0; i < 1000; i++) {
        char c = (char)('a' + i % 26);
        ring_append(&r, &c, 1);
    }
    flatten(&r, 100, out, &dropped);
    /* head = first 2 chars "ab"; tail = last 8 of the stream (i = 992..999). */
    char want[11] = "ab";
    for (int i = 992; i < 1000; i++) {
        want[2 + i - 992] = (char)('a' + i % 26);
    }
    want[10] = 0;
    CHECK_STR(out, want);
    CHECK_EQ(dropped, 990);

    return CHECK_DONE();
}
