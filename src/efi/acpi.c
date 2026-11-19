/* ACPI table discovery: configuration table -> RSDP -> XSDT/RSDT -> tables. */
#include "efi/acpi.h"

#include <string.h>

#include "efi/efi.h"

#include <Guid/Acpi.h>

#include "efi/log.h"

EFI_GUID gEfiAcpi20TableGuid = EFI_ACPI_20_TABLE_GUID;
EFI_GUID gEfiAcpi10TableGuid = ACPI_10_TABLE_GUID;

static const uint8_t *sdt;
static size_t sdt_len;
static int sdt_is_x;
static bool inited;

/* Tables are in firmware-reserved memory, identity mapped during boot services.
 * Lengths are capped to keep a corrupt header from making us read gigabytes. */
#define ACPI_MAX_TABLE (4u << 20)

static const uint8_t *map_table(uint64_t addr, size_t *len) {
    if (!addr) {
        return NULL;
    }
    const uint8_t *t = (const uint8_t *)(uintptr_t)addr;
    uint32_t l = acpi_table_len(t, ACPI_HDR_LEN);
    if (l < ACPI_HDR_LEN || l > ACPI_MAX_TABLE || !acpi_checksum_ok(t, l)) {
        log_msg(LOG_WARN, "acpi: invalid table '%.4s' at 0x%llx (len %u)", (const char *)t,
                (unsigned long long)addr, l);
        return NULL;
    }
    *len = l;
    return t;
}

bool acpi_init(void) {
    if (inited) {
        return sdt != NULL;
    }
    inited = true;
    const uint8_t *rsdp = NULL;
    for (int pass = 0; pass < 2 && !rsdp; pass++) {
        EFI_GUID *want = pass == 0 ? &gEfiAcpi20TableGuid : &gEfiAcpi10TableGuid;
        for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
            if (memcmp(&gST->ConfigurationTable[i].VendorGuid, want, sizeof *want) == 0) {
                rsdp = gST->ConfigurationTable[i].VendorTable;
                break;
            }
        }
    }
    if (!rsdp || memcmp(rsdp, "RSD PTR ", 8) != 0 || !acpi_checksum_ok(rsdp, 20)) {
        log_msg(LOG_WARN, "acpi: no valid RSDP");
        return false;
    }
    uint8_t rev = rsdp[15];
    uint64_t xsdt = 0;
    uint32_t rsdt = 0;
    memcpy(&rsdt, rsdp + 16, 4);
    if (rev >= 2 && acpi_checksum_ok(rsdp, 36)) {
        memcpy(&xsdt, rsdp + 24, 8);
    }
    if (xsdt && (sdt = map_table(xsdt, &sdt_len)) && memcmp(sdt, "XSDT", 4) == 0) {
        sdt_is_x = 1;
    } else if ((sdt = map_table(rsdt, &sdt_len)) && memcmp(sdt, "RSDT", 4) == 0) {
        sdt_is_x = 0;
    } else {
        sdt = NULL;
        log_msg(LOG_WARN, "acpi: no valid XSDT/RSDT");
        return false;
    }
    log_msg(LOG_INFO, "acpi: RSDP rev %u, %s with %zu entries", rev, sdt_is_x ? "XSDT" : "RSDT",
            acpi_sdt_count(sdt_len, sdt_is_x));
    return true;
}

const uint8_t *acpi_find(const char *sig, unsigned idx, size_t *len) {
    if (!acpi_init()) {
        return NULL;
    }
    if (memcmp(sig, "DSDT", 4) == 0) {
        acpi_fadt_info f;
        return idx == 0 && acpi_fadt(&f) ? map_table(f.dsdt, len) : NULL;
    }
    size_t n = acpi_sdt_count(sdt_len, sdt_is_x);
    for (size_t i = 0; i < n; i++) {
        uint64_t addr = acpi_sdt_entry(sdt, i, sdt_is_x);
        const uint8_t *t = addr ? (const uint8_t *)(uintptr_t)addr : NULL;
        if (t && memcmp(t, sig, 4) == 0 && (t = map_table(addr, len)) && idx-- == 0) {
            return t;
        }
    }
    return NULL;
}

bool acpi_fadt(acpi_fadt_info *out) {
    size_t len;
    const uint8_t *t = acpi_find("FACP", 0, &len);
    return t && acpi_parse_fadt(t, len, out) == 0;
}

bool acpi_hpet_base(uint64_t *base) {
    size_t len;
    const uint8_t *t = acpi_find("HPET", 0, &len);
    return t && acpi_parse_hpet(t, len, base) == 0;
}

int acpi_eisaid_count(const char *id) {
    size_t len;
    int n = 0;
    const uint8_t *t = acpi_find("DSDT", 0, &len);
    if (t) {
        n += acpi_count_eisaid(t, len, id);
    }
    for (unsigned i = 0; (t = acpi_find("SSDT", i, &len)); i++) {
        n += acpi_count_eisaid(t, len, id);
    }
    return n;
}

void acpi_for_each(void (*fn)(const char *sig, const uint8_t *t, size_t len, void *ctx),
                   void *ctx) {
    size_t len;
    const uint8_t *t;
    if (!acpi_init()) {
        return;
    }
    if ((t = acpi_find("DSDT", 0, &len))) {
        fn("DSDT", t, len, ctx);
    }
    size_t n = acpi_sdt_count(sdt_len, sdt_is_x);
    for (size_t i = 0; i < n; i++) {
        if ((t = map_table(acpi_sdt_entry(sdt, i, sdt_is_x), &len))) {
            char sig[5] = {(char)t[0], (char)t[1], (char)t[2], (char)t[3], 0};
            fn(sig, t, len, ctx);
        }
    }
}
