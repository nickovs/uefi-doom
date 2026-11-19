/* The libc shim's pure parts, built as shim_* and compared with the host libc. */
#include <ctype.h>
#include <stdlib.h>

#include "check.h"
#include "libc_pure.h"

static int near(double a, double b) {
    double d = a - b;
    return d < 1e-9 * (b < 0 ? -b : b) + 1e-12 && -d < 1e-9 * (b < 0 ? -b : b) + 1e-12;
}

int main(void) {
    for (int c = -1; c < 256; c++) {
        CHECK_EQ(!!shim_isspace(c), c >= 0 && c < 128 && !!isspace(c));
        CHECK_EQ(!!shim_isprint(c), c >= 0 && c < 128 && !!isprint(c));
        CHECK_EQ(!!shim_isalnum(c), c >= 0 && c < 128 && !!isalnum(c));
        CHECK_EQ(!!shim_ispunct(c), c >= 0 && c < 128 && !!ispunct(c));
        CHECK_EQ(!!shim_isxdigit(c), c >= 0 && c < 128 && !!isxdigit(c));
        if (c >= 0 && c < 128) {
            CHECK_EQ(shim_toupper(c), toupper(c));
            CHECK_EQ(shim_tolower(c), tolower(c));
        }
    }

    char buf[32];
    CHECK_STR(shim_strcpy(buf, "abc"), "abc");
    CHECK_STR(shim_strcat(buf, "def"), "abcdef");
    char *d = shim_strdup("dup");
    CHECK_STR(d, "dup");
    free(d);
    CHECK(shim_strcasecmp("DOOM1.WAD", "doom1.wad") == 0);
    CHECK(shim_strcasecmp("a", "B") < 0);
    CHECK(shim_strcasecmp("abc", "ab") > 0);
    CHECK(shim_strncasecmp("E1M1x", "e1m1y", 4) == 0);
    CHECK(shim_strncasecmp("E1M1x", "e1m1y", 5) < 0);
    CHECK_STR(shim_strerror(2), "error 2");
    CHECK_EQ(shim_abs(-7), 7);

    const char *nums[] = {"0", "1.5", "-2.25", "  3e2", "1e-3", "100.0", ".5", "7.", "1.000000"};
    for (size_t i = 0; i < sizeof nums / sizeof nums[0]; i++) {
        char *e1, *e2;
        double a = shim_strtod(nums[i], &e1), b = strtod(nums[i], &e2);
        CHECK(near(a, b));
        CHECK_EQ(e1 - nums[i], e2 - nums[i]);
    }
    char *e;
    CHECK_EQ(shim_strtod("x", &e), 0);
    CHECK_STR(e, "x");
    CHECK(near(shim_atof("0.75"), 0.75));
    CHECK_EQ(shim_strtoul("0x1f", &e, 0), 31);
    CHECK_EQ(shim_strtoul("077", NULL, 0), 63);
    CHECK_EQ(shim_strtoul("zz", &e, 10), 0);
    CHECK_STR(e, "zz");

    /* sscanf: the engine's M_StrToInt patterns, in order. */
    int v = 0;
    CHECK_EQ(shim_sscanf("0x1F", " 0x%x", &v), 1);
    CHECK_EQ(v, 31);
    CHECK_EQ(shim_sscanf("17", " 0x%x", &v), 0);
    CHECK_EQ(shim_sscanf("017", " 0%o", &v), 1);
    CHECK_EQ(v, 15);
    CHECK_EQ(shim_sscanf("  -42", " %d", &v), 1);
    CHECK_EQ(v, -42);
    CHECK_EQ(shim_sscanf("", " %d", &v), -1);
    CHECK_EQ(shim_sscanf("abc", "%d", &v), 0);

    int a = 0, b = 0, n = 0;
    char s[16], c2[3] = {0};
    CHECK_EQ(shim_sscanf("12,34 rest", "%d,%d%n", &a, &b, &n), 2);
    CHECK_EQ(a, 12);
    CHECK_EQ(b, 34);
    CHECK_EQ(n, 5);
    CHECK_EQ(shim_sscanf("key value", "%15s %2c", s, c2), 2);
    CHECK_STR(s, "key");
    CHECK_STR(c2, "va");
    CHECK_EQ(shim_sscanf("12345", "%3d%d", &a, &b), 2);
    CHECK_EQ(a, 123);
    CHECK_EQ(b, 45);
    CHECK_EQ(shim_sscanf("5 6", "%*d %i", &a), 1);
    CHECK_EQ(a, 6);
    CHECK_EQ(shim_sscanf("50%", "%d%%", &a), 1);
    short h = 0;
    CHECK_EQ(shim_sscanf("-3", "%hd", &h), 1);
    CHECK_EQ(h, -3);
    CHECK_EQ(shim_sscanf("1", "%f", &a), 0); /* unsupported: short count */

    /* Scansets, as m_config.c reads "name value" lines. */
    char name[80], val[100];
    CHECK_EQ(shim_sscanf("key_up 119\n", "%79s %99[^\n]\n", name, val), 2);
    CHECK_STR(name, "key_up");
    CHECK_STR(val, "119");
    CHECK_EQ(shim_sscanf("chatmacro0 \"no help here\"\r\n", "%79s %99[^\n]\n", name, val), 2);
    CHECK_STR(val, "\"no help here\"\r");
    CHECK_EQ(shim_sscanf("lonely\n", "%79s %99[^\n]\n", name, val), 1);
    CHECK_EQ(shim_sscanf("abc123", "%[a-c]", val), 0 + 1); /* '-' literal: "a" matches */
    CHECK_STR(val, "a");
    CHECK_EQ(shim_sscanf("]]x", "%[]]", val), 1);
    CHECK_STR(val, "]]");
    CHECK_EQ(shim_sscanf("abcdef", "%3[a-f]", val), 1);
    CHECK_STR(val, "a");
    CHECK_EQ(shim_sscanf("xyz", "%[abc]", val), 0);
    CHECK_EQ(shim_sscanf("ab", "%[ab", val), 0); /* unterminated set */
    return CHECK_DONE();
}
