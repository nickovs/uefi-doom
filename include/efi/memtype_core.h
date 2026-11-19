/* x86 memory-type logic: MTRR range matching, PAT decoding and the MTRR x PAT
 * combination (Intel SDM vol. 3, "Selecting Memory Types for Pentium III and
 * More Recent Processor Families"). Pure code, unit-tested on the host. */
#ifndef UD_EFI_MEMTYPE_CORE_H
#define UD_EFI_MEMTYPE_CORE_H

#include <stdint.h>

enum {
    MT_UC = 0,
    MT_WC = 1,
    MT_WT = 4,
    MT_WP = 5,
    MT_WB = 6,
    MT_UC_MINUS = 7,  /* PAT only */
    MT_CONFLICT = -2, /* overlapping variable MTRRs with incompatible types */
};

/**
 * MTRR memory type for physical address `addr` (above 1 MiB: fixed-range
 * MTRRs are ignored). `def_type` is IA32_MTRR_DEF_TYPE; bases/masks are the
 * IA32_MTRR_PHYSBASEn / PHYSMASKn pairs; `phys_bits` from CPUID 0x80000008.
 */
int mtrr_type_for(uint64_t addr, uint64_t def_type, const uint64_t *bases, const uint64_t *masks,
                  int n, int phys_bits);

/** Memory type of PAT entry `idx` (0..7) in an IA32_PAT value. */
int pat_type(uint64_t pat_msr, int idx);

/** PAT index from page-table bits: PAT << 2 | PCD << 1 | PWT. */
int pat_index(int pat, int pcd, int pwt);

/** Effective type from an MTRR type and a PAT type; MT_CONFLICT propagates. */
int memtype_effective(int mtrr, int pat);

/** "UC", "WC", "WT", "WP", "WB", "UC-", "conflict" or "?" */
const char *memtype_name(int t);

#endif
