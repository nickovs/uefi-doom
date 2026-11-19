#include "check.h"
#include "efi/argv_core.h"

/* Build a UCS-2 string from ASCII (with optional terminating NUL). */
static size_t u16(const char *s, uint16_t *out, int nul) {
    size_t n = 0;
    while (s[n]) {
        out[n] = (uint8_t)s[n];
        n++;
    }
    if (nul) {
        out[n++] = 0;
    }
    return n * 2;
}

int main(void) {
    uint16_t w[128];
    char buf[256];
    char *argv[8];

    /* Text detection. */
    CHECK(argv_is_text(w, u16("-warp 1 1", w, 1)));
    CHECK(argv_is_text(w, u16("-warp 1 1", w, 0))); /* efibootmgr may omit the NUL */
    CHECK(!argv_is_text(w, 0));
    CHECK(!argv_is_text(w, 3)); /* odd size */
    uint16_t bin[] = {'W', 'I', 0x0001, 0x8000};
    CHECK(!argv_is_text(bin, sizeof bin));
    uint16_t surrogate[] = {'a', 0xd800, 0};
    CHECK(!argv_is_text(surrogate, sizeof surrogate));

    /* Tokenising with quotes and repeated whitespace. */
    size_t sz = u16("  -warp 1  1\t-file \"my file.wad\"", w, 1);
    int argc = argv_from_load_options(w, sz, "doom", buf, sizeof buf, argv, 8);
    CHECK_EQ(argc, 6);
    CHECK_STR(argv[0], "doom");
    CHECK_STR(argv[1], "-warp");
    CHECK_STR(argv[3], "1");
    CHECK_STR(argv[5], "my file.wad");
    CHECK(argv[6] == NULL);

    /* Binary options: just the program name. */
    argc = argv_from_load_options(bin, sizeof bin, "doom", buf, sizeof buf, argv, 8);
    CHECK_EQ(argc, 1);
    CHECK(argv[1] == NULL);

    /* No options at all. */
    CHECK_EQ(argv_from_load_options(NULL, 0, "hwprobe", buf, sizeof buf, argv, 8), 1);

    /* Too many args / too small buffer are reported, not overflowed. */
    sz = u16("a b c d e f g h i", w, 1);
    CHECK_EQ(argv_from_load_options(w, sz, "doom", buf, sizeof buf, argv, 8), -1);
    sz = u16("abcdefghij", w, 1);
    CHECK_EQ(argv_from_load_options(w, sz, "doom", buf, 10, argv, 8), -1);

    /* UTF-8 conversion: 2- and 3-byte sequences. */
    uint16_t uni[] = {'A', 0x00e9, 0x20ac, 0};
    char u8[16];
    CHECK_EQ(argv_ucs2_to_utf8(uni, 8, u8, sizeof u8), 6);
    CHECK_STR(u8, "A\xc3\xa9\xe2\x82\xac");
    CHECK_EQ(argv_ucs2_to_utf8(uni, 8, u8, 4), -1); /* does not fit */
    CHECK_EQ(u8[3], 0);                             /* still terminated */

    return CHECK_DONE();
}
