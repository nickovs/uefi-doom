#include "check.h"
#include "efi/acpi_core.h"

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static void header(uint8_t *t, const char *sig, uint32_t len, uint8_t rev) {
    memset(t, 0, len);
    memcpy(t, sig, 4);
    put32(t + 4, len);
    t[8] = rev;
}

static void fix_checksum(uint8_t *t, uint32_t len) {
    uint8_t sum = 0;
    t[9] = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + t[i]);
    }
    t[9] = (uint8_t)(0 - sum);
}

int main(void) {
    /* FADT shaped like a real laptop's: rev 6, 24-bit PM timer at 0x1808 via
     * X_PM_TMR_BLK, IAPC_BOOT_ARCH = 0 (8042 flag clear), flags 0xca5. */
    uint8_t fadt[276];
    header(fadt, "FACP", sizeof fadt, 6);
    put32(fadt + 76, 0x1808);
    fadt[91] = 4;
    put32(fadt + 112, 0x00000ca5);
    put32(fadt + 40, 0x11110000);             /* DSDT */
    put64(fadt + 140, 0x2222000033330000ull); /* X_DSDT wins */
    fadt[208] = 1;                            /* X_PM_TMR_BLK: I/O space */
    put64(fadt + 212, 0x1808);
    fix_checksum(fadt, sizeof fadt);

    CHECK(acpi_table_valid(fadt, sizeof fadt));
    acpi_fadt_info f;
    CHECK_EQ(acpi_parse_fadt(fadt, sizeof fadt, &f), 0);
    CHECK_EQ(f.pm_tmr_port, 0x1808);
    CHECK_EQ(f.pm_tmr_32bit, 0);
    CHECK_EQ(f.iapc_boot_arch & ACPI_IAPC_8042, 0);
    CHECK_EQ(f.hw_reduced, 0);
    CHECK_EQ(f.dsdt, 0x2222000033330000ull);

    /* Corrupt checksum / truncated table / wrong signature are rejected. */
    fadt[100] ^= 1;
    CHECK(!acpi_table_valid(fadt, sizeof fadt));
    fadt[100] ^= 1;
    CHECK(!acpi_table_valid(fadt, 200)); /* header claims more than available */
    CHECK_EQ(acpi_parse_fadt(fadt, 100, &f), -1);
    memcpy(fadt, "XXXX", 4);
    CHECK_EQ(acpi_parse_fadt(fadt, sizeof fadt, &f), -1);

    /* ACPI 1.0-sized FADT: legacy PM_TMR_BLK, no IAPC field, 32-bit timer. */
    uint8_t old[116];
    header(old, "FACP", sizeof old, 1);
    put32(old + 76, 0x408);
    old[91] = 4;
    put32(old + 112, 1u << 8);
    CHECK_EQ(acpi_parse_fadt(old, sizeof old, &f), 0);
    CHECK_EQ(f.pm_tmr_port, 0x408);
    CHECK_EQ(f.pm_tmr_32bit, 1);
    CHECK_EQ(f.iapc_boot_arch, 0);

    /* HPET */
    uint8_t hpet[56];
    header(hpet, "HPET", sizeof hpet, 1);
    put64(hpet + 44, 0xfed00000);
    uint64_t base = 0;
    CHECK_EQ(acpi_parse_hpet(hpet, sizeof hpet, &base), 0);
    CHECK_EQ(base, 0xfed00000);
    hpet[40] = 1; /* I/O space: not supported */
    CHECK_EQ(acpi_parse_hpet(hpet, sizeof hpet, &base), -1);

    /* EISA ids: PNP0303 encodes to bytes 41 D0 03 03. */
    CHECK_EQ(acpi_eisaid("PNP0303"), 0x0303d041);
    CHECK_EQ(acpi_eisaid("PNP0C50"), 0x500cd041);
    uint8_t dsdt[64];
    header(dsdt, "DSDT", sizeof dsdt, 2);
    const uint8_t aml[] = {0x08, 0x5f, 0x48, 0x49, 0x44,
                           0x0c, 0x41, 0xd0, 0x03, 0x03, /* Name(_HID, EISAID) */
                           0x41, 0xd0, 0x03, 0x03 /* bare bytes without prefix: not counted */};
    memcpy(dsdt + 40, aml, sizeof aml);
    CHECK_EQ(acpi_count_eisaid(dsdt, sizeof dsdt, "PNP0303"), 1);
    CHECK_EQ(acpi_count_eisaid(dsdt, sizeof dsdt, "PNP0800"), 0);

    /* XSDT / RSDT entry walking. */
    uint8_t xsdt[36 + 16];
    header(xsdt, "XSDT", sizeof xsdt, 1);
    put64(xsdt + 36, 0x1000);
    put64(xsdt + 44, 0x123456789ull);
    CHECK_EQ(acpi_sdt_count(sizeof xsdt, 1), 2);
    CHECK_EQ(acpi_sdt_entry(xsdt, 1, 1), 0x123456789ull);
    CHECK_EQ(acpi_sdt_count(sizeof xsdt, 0), 4); /* same bytes as RSDT */

    return CHECK_DONE();
}
