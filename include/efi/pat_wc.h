/* Write-combining for a physical range by programming the CPU directly: a PAT
 * entry set to WC plus page-table entries selecting it (PAT WC overrides MTRR
 * UC). Used when firmware refuses SetMemorySpaceAttributes(EFI_MEMORY_WC).
 * Everything is undone by pat_wc_restore(), which is also registered as a
 * cleanup. Boot processor only: the APs are parked in firmware and never touch
 * the range. Requires 4-level paging and an identity-mapped range. */
#ifndef UD_EFI_PAT_WC_H
#define UD_EFI_PAT_WC_H

#include <stddef.h>
#include <stdint.h>

/**
 * Map [base, base+size) as WC. A 1 GiB page containing the range is split into
 * 2 MiB pages so only the range's pages change; the range is widened to whole
 * 2 MiB pages. Returns 0, or -1 with a reason in `why`.
 */
int pat_wc_enable(uint64_t base, uint64_t size, char *why, size_t why_len);

/** Undo pat_wc_enable (also runs at exit). */
void pat_wc_restore(void);

#endif
