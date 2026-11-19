/* ACPI table parsing over byte buffers. Pure code: no UEFI dependencies,
 * unit-tested on the host (including tables captured from real machines).
 * See docs/architecture.md#acpi. */
#ifndef UD_EFI_ACPI_CORE_H
#define UD_EFI_ACPI_CORE_H

#include <stddef.h>
#include <stdint.h>

#define ACPI_HDR_LEN 36

typedef struct {
    uint8_t revision;
    uint16_t pm_tmr_port;    /* 0 if no I/O-space PM timer */
    uint8_t pm_tmr_32bit;    /* FADT flag TMR_VAL_EXT */
    uint16_t iapc_boot_arch; /* 0 if the table predates the field */
    uint32_t flags;
    uint8_t hw_reduced; /* FADT flag HW_REDUCED_ACPI */
    uint64_t dsdt;      /* physical address (X_DSDT preferred) */
} acpi_fadt_info;

#define ACPI_IAPC_LEGACY_DEVICES (1u << 0)
#define ACPI_IAPC_8042 (1u << 1)
#define ACPI_IAPC_VGA_NOT_PRESENT (1u << 2)
#define ACPI_IAPC_CMOS_RTC_NOT_PRESENT (1u << 5)

/** Length from a table header, or 0 if `avail` bytes cannot hold a header. */
uint32_t acpi_table_len(const uint8_t *t, size_t avail);

/** 1 if `len` bytes sum to zero modulo 256. */
int acpi_checksum_ok(const uint8_t *t, size_t len);

/** 1 if the table has a sane length (>= header, <= avail) and a valid checksum. */
int acpi_table_valid(const uint8_t *t, size_t avail);

/** Decode the fields we use from a FADT ("FACP"). Returns 0 on success. */
int acpi_parse_fadt(const uint8_t *t, size_t len, acpi_fadt_info *out);

/** Decode the HPET MMIO base address. Returns 0 on success. */
int acpi_parse_hpet(const uint8_t *t, size_t len, uint64_t *base);

/** Compressed EISA ID for a 7-character id such as "PNP0303", as stored in AML. */
uint32_t acpi_eisaid(const char *id);

/**
 * Count AML DWordConst encodings (0x0C + 4 bytes) of EISA id `id` in the body
 * of a DSDT/SSDT. A hint only: AML is not interpreted.
 */
int acpi_count_eisaid(const uint8_t *t, size_t len, const char *id);

/** Number of entries in an XSDT (8-byte) or RSDT (4-byte) table. */
size_t acpi_sdt_count(size_t len, int is_xsdt);

/** Physical address of entry `i` of an XSDT/RSDT. */
uint64_t acpi_sdt_entry(const uint8_t *t, size_t i, int is_xsdt);

#endif
