#include "check.h"
#include "efi/dlog_core.h"

static void name_to_ascii(const uint16_t *n, char *out) {
    while ((*out++ = (char)*n++)) {
    }
}

int main(void) {
    uint16_t name[11];
    char ascii[16];

    dlog_chunk_name(0, name);
    name_to_ascii(name, ascii);
    CHECK_STR(ascii, "DoomLog000");
    dlog_chunk_name(42, name);
    name_to_ascii(name, ascii);
    CHECK_STR(ascii, "DoomLog042");
    dlog_chunk_name(999, name);
    name_to_ascii(name, ascii);
    CHECK_STR(ascii, "DoomLog999");

    size_t chunk, budget;
    dlog_size_plan(0, 0, &chunk, &budget); /* unknown: defaults */
    CHECK_EQ(chunk, DLOG_CHUNK_CAP);
    CHECK_EQ(budget, DLOG_BUDGET_CAP);
    dlog_size_plan(8192, 1024 * 1024, &chunk, &budget); /* small max var */
    CHECK_EQ(chunk, 8192 - DLOG_VAR_OVERHEAD);
    CHECK_EQ(budget, DLOG_BUDGET_CAP);
    dlog_size_plan(1 << 20, 64 * 1024, &chunk, &budget); /* little space left */
    CHECK_EQ(chunk, DLOG_CHUNK_CAP);
    CHECK_EQ(budget, 32 * 1024);
    dlog_size_plan(200, 0, &chunk, &budget); /* absurdly small: clamp to min */
    CHECK_EQ(chunk, DLOG_CHUNK_MIN);

    dlog_info info;
    dlog_info_fill(&info, 1, 0x8000000000000001ull, 3, 1024, 2500, 7, 100, 123456, "v1.2-devloop");
    CHECK(memcmp(info.magic, "DLOG", 4) == 0);
    CHECK_EQ(info.version, DLOG_VERSION);
    CHECK_EQ(info.chunk_count, 3);
    CHECK_EQ(info.head_bytes, 100);
    CHECK_STR(info.build_id, "v1.2-devloop");
    CHECK_EQ(info.build_id[63], 0);

    /* Over-long build ids are truncated but stay NUL-terminated. */
    char longid[100];
    memset(longid, 'x', sizeof longid - 1);
    longid[99] = 0;
    dlog_info_fill(&info, 0, 0, 0, 0, 0, 0, 0, 0, longid);
    CHECK_EQ(strlen(info.build_id), 63);

    return CHECK_DONE();
}
