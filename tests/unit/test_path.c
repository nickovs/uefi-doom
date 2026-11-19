#include "check.h"
#include "efi/path_core.h"

static const char *a(const uint16_t *w) {
    static char buf[300];
    size_t i = 0;
    for (; w[i] && i < sizeof buf - 1; i++) {
        buf[i] = w[i] < 0x80 ? (char)w[i] : '?';
    }
    buf[i] = 0;
    return buf;
}

static const uint16_t base[] = {'\\', 'E', 'F', 'I', '\\', 'D', 'O', 'O', 'M', 0};

int main(void) {
    uint16_t out[PATH_MAX_UNITS + 1];

    CHECK_EQ(path_translate("doom1.wad", base, out), 0);
    CHECK_STR(a(out), "\\EFI\\DOOM\\doom1.wad");
    CHECK_EQ(path_translate("./saves/../doomsav0.dsg", base, out), 0);
    CHECK_STR(a(out), "\\EFI\\DOOM\\doomsav0.dsg");
    CHECK_EQ(path_translate("/EFI/BOOT//BOOTX64.EFI", base, out), 0);
    CHECK_STR(a(out), "\\EFI\\BOOT\\BOOTX64.EFI");
    CHECK_EQ(path_translate(".", base, out), 0);
    CHECK_STR(a(out), "\\EFI\\DOOM");
    CHECK_EQ(path_translate("", base, out), 0);
    CHECK_STR(a(out), "\\EFI\\DOOM");
    CHECK_EQ(path_translate("../../x", base, out), 0);
    CHECK_STR(a(out), "\\x");
    CHECK_EQ(path_translate("../../../x", base, out), -1); /* above root */
    CHECK_EQ(path_translate("/..", base, out), -1);
    CHECK_EQ(path_translate("\\a\\b", base, out), 0);
    CHECK_STR(a(out), "\\a\\b");

    /* UTF-8: e-acute becomes one UCS-2 unit; bad sequences rejected. */
    CHECK_EQ(path_translate("caf\xc3\xa9", base, out), 0);
    CHECK_EQ(out[13], 0x00e9);
    CHECK_EQ(path_translate("bad\xc3", base, out), -1);
    CHECK_EQ(path_translate("\xf0\x9f\x98\x80", base, out), -1); /* non-BMP */
    CHECK_EQ(path_translate("\xc0\xaf", base, out), -1);         /* overlong '/' */

    /* Length limit. */
    char longp[400];
    memset(longp, 'x', sizeof longp - 1);
    longp[399] = 0;
    CHECK_EQ(path_translate(longp, base, out), -1);

    /* dirname */
    uint16_t d[64];
    const uint16_t f1[] = {'\\', 'E', 'F', 'I', '\\', 'D', 'O', 'O', 'M', '\\', 'X', 0};
    CHECK_EQ(path_dirname(f1, d, 64), 0);
    CHECK_STR(a(d), "\\EFI\\DOOM");
    const uint16_t f2[] = {'\\', 'X', 0};
    CHECK_EQ(path_dirname(f2, d, 64), 0);
    CHECK_STR(a(d), "\\");
    CHECK_EQ(path_dirname(f1, d, 4), -1);

    return CHECK_DONE();
}
