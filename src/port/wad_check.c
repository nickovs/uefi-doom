/* WAD validation. See include/port/wad_check.h. */
#include "port/wad_check.h"

#include <stdio.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int wad_check(const uint8_t *buf, size_t len, char *why, size_t why_len) {
    if (len < 12) {
        snprintf(why, why_len, "file too short for a WAD header (%zu bytes)", len);
        return -1;
    }
    if (len > 0xffffffffu) {
        snprintf(why, why_len, "file larger than 4 GiB");
        return -1;
    }
    if (memcmp(buf, "IWAD", 4) != 0 && memcmp(buf, "PWAD", 4) != 0) {
        snprintf(why, why_len, "not a WAD (bad magic)");
        return -1;
    }
    uint32_t count = le32(buf + 4), dir = le32(buf + 8);
    if (count > 0x7fffffffu / 16 || dir > len || (uint64_t)count * 16 > len - dir) {
        snprintf(why, why_len, "directory (%u entries at %u) outside the file", count, dir);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = buf + dir + (size_t)i * 16;
        uint32_t pos = le32(e), size = le32(e + 4);
        if (size && (pos > len || size > len - pos)) {
            snprintf(why, why_len, "lump %u (%.8s) at %u+%u outside the file", i,
                     (const char *)e + 8, pos, size);
            return -1;
        }
    }
    return 0;
}
