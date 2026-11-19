/* Access to the firmware's ACPI tables (located via the UEFI configuration table). */
#ifndef UD_EFI_ACPI_H
#define UD_EFI_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "efi/acpi_core.h"

/** Locate and validate RSDP and XSDT/RSDT. Safe to call more than once. */
bool acpi_init(void);

/**
 * Return the `idx`th valid table with signature `sig` (e.g. "SSDT", 1), or NULL.
 * "DSDT" is found through the FADT. Invalid tables (bad length/checksum) are
 * skipped with a warning.
 */
const uint8_t *acpi_find(const char *sig, unsigned idx, size_t *len);

/** Decoded FADT, or false if absent/invalid. */
bool acpi_fadt(acpi_fadt_info *out);

/** HPET MMIO base, or false if absent. */
bool acpi_hpet_base(uint64_t *base);

/** Count of `id` (e.g. "PNP0303") in the DSDT and all SSDTs (hint only). */
int acpi_eisaid_count(const char *id);

/** Visit every valid table (including the DSDT); for dumping raw tables. */
void acpi_for_each(void (*fn)(const char *sig, const uint8_t *t, size_t len, void *ctx), void *ctx);

#endif
