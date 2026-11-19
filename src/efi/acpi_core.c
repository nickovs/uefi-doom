#include "efi/acpi_core.h"

#include <string.h>

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

uint32_t acpi_table_len(const uint8_t *t, size_t avail) {
    return avail < ACPI_HDR_LEN ? 0 : rd32(t + 4);
}

int acpi_checksum_ok(const uint8_t *t, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + t[i]);
    }
    return sum == 0;
}

int acpi_table_valid(const uint8_t *t, size_t avail) {
    uint32_t len = acpi_table_len(t, avail);
    return len >= ACPI_HDR_LEN && len <= avail && acpi_checksum_ok(t, len);
}

/* FADT field offsets (ACPI 6.x, section 5.2.9). */
enum {
    FADT_DSDT = 40,
    FADT_PM_TMR_BLK = 76,
    FADT_PM_TMR_LEN = 91,
    FADT_IAPC_BOOT_ARCH = 109,
    FADT_FLAGS = 112,
    FADT_X_DSDT = 140,
    FADT_X_PM_TMR_BLK = 208, /* Generic Address Structure: space id, width, offset, size, addr */
};
#define FADT_FLAG_TMR_VAL_EXT (1u << 8)
#define FADT_FLAG_HW_REDUCED (1u << 20)
#define GAS_SPACE_IO 1

int acpi_parse_fadt(const uint8_t *t, size_t len, acpi_fadt_info *out) {
    if (len < 116 || memcmp(t, "FACP", 4) != 0) {
        return -1; /* need at least through Flags (ACPI 1.0 FADT is 116 bytes) */
    }
    memset(out, 0, sizeof *out);
    out->revision = t[8];
    out->flags = rd32(t + FADT_FLAGS);
    out->pm_tmr_32bit = (out->flags & FADT_FLAG_TMR_VAL_EXT) != 0;
    out->hw_reduced = (out->flags & FADT_FLAG_HW_REDUCED) != 0;
    if (out->revision >= 2) {
        out->iapc_boot_arch = rd16(t + FADT_IAPC_BOOT_ARCH);
    }
    out->dsdt = rd32(t + FADT_DSDT);
    if (len >= FADT_X_DSDT + 8 && rd64(t + FADT_X_DSDT)) {
        out->dsdt = rd64(t + FADT_X_DSDT);
    }
    if (len >= FADT_X_PM_TMR_BLK + 12 && t[FADT_X_PM_TMR_BLK] == GAS_SPACE_IO &&
        rd64(t + FADT_X_PM_TMR_BLK + 4) && rd64(t + FADT_X_PM_TMR_BLK + 4) <= 0xffff) {
        out->pm_tmr_port = (uint16_t)rd64(t + FADT_X_PM_TMR_BLK + 4);
    } else if (t[FADT_PM_TMR_LEN] == 4 && rd32(t + FADT_PM_TMR_BLK) &&
               rd32(t + FADT_PM_TMR_BLK) <= 0xffff) {
        out->pm_tmr_port = (uint16_t)rd32(t + FADT_PM_TMR_BLK);
    }
    return 0;
}

int acpi_parse_hpet(const uint8_t *t, size_t len, uint64_t *base) {
    /* Header (36) + event timer block id (4) + base address GAS (12). */
    if (len < 52 || memcmp(t, "HPET", 4) != 0 || t[40] != 0 /* memory space */) {
        return -1;
    }
    *base = rd64(t + 44);
    return *base ? 0 : -1;
}

uint32_t acpi_eisaid(const char *id) {
    /* Three letters packed 5 bits each (A = 1), big-endian, then 4 hex digits. */
    uint16_t v = (uint16_t)(((id[0] - '@') & 0x1f) << 10 | ((id[1] - '@') & 0x1f) << 5 |
                            ((id[2] - '@') & 0x1f));
    uint32_t hex = 0;
    for (int i = 3; i < 7; i++) {
        char c = id[i];
        hex = hex << 4 | (uint32_t)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    /* Stored little-endian as bytes [v_hi, v_lo, hex_hi, hex_lo]. */
    return (uint32_t)(v >> 8) | (uint32_t)(v & 0xff) << 8 | (hex >> 8 & 0xff) << 16 |
           (hex & 0xff) << 24;
}

int acpi_count_eisaid(const uint8_t *t, size_t len, const char *id) {
    uint32_t want = acpi_eisaid(id);
    int n = 0;
    for (size_t i = ACPI_HDR_LEN; i + 5 <= len; i++) {
        if (t[i] == 0x0c && rd32(t + i + 1) == want) {
            n++;
        }
    }
    return n;
}

size_t acpi_sdt_count(size_t len, int is_xsdt) {
    return len < ACPI_HDR_LEN ? 0 : (len - ACPI_HDR_LEN) / (is_xsdt ? 8 : 4);
}

uint64_t acpi_sdt_entry(const uint8_t *t, size_t i, int is_xsdt) {
    const uint8_t *p = t + ACPI_HDR_LEN + i * (is_xsdt ? 8 : 4);
    return is_xsdt ? rd64(p) : rd32(p);
}
