#include <stdint.h>
#include <string.h>

#include "check.h"
#include "port/wad_check.h"

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int main(void) {
    char why[96];
    /* Header, one 4-byte lump at 12, directory at 16. */
    uint8_t w[32] = "PWAD";
    put32(w + 4, 1);
    put32(w + 8, 16);
    memcpy(w + 12, "DATA", 4);
    put32(w + 16, 12);
    put32(w + 20, 4);
    memcpy(w + 24, "LUMP", 4);
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), 0);
    memcpy(w, "IWAD", 4);
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), 0);

    CHECK_EQ(wad_check(w, 11, why, sizeof why), -1);
    CHECK(strstr(why, "too short"));
    CHECK_EQ(wad_check(w, 31, why, sizeof why), -1); /* directory runs off the end */
    CHECK(strstr(why, "directory"));

    memcpy(w, "JUNK", 4);
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), -1);
    CHECK(strstr(why, "magic"));
    memcpy(w, "PWAD", 4);

    put32(w + 20, 21); /* lump runs 1 byte past the end */
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), -1);
    CHECK(strstr(why, "lump 0 (LUMP)"));
    put32(w + 16, 0xfffffff0u); /* offset + size would wrap */
    put32(w + 20, 0x20);
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), -1);
    put32(w + 16, 0xffffffffu); /* zero-size lumps may point anywhere (markers) */
    put32(w + 20, 0);
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), 0);

    put32(w + 4, 0x10000000u); /* huge count: no overflow */
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), -1);
    put32(w + 4, 0);
    put32(w + 8, 33); /* empty directory but offset past the end */
    CHECK_EQ(wad_check(w, sizeof w, why, sizeof why), -1);
    return CHECK_DONE();
}
