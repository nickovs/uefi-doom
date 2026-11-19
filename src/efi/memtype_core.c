#include "efi/memtype_core.h"

#define MTRR_DEF_ENABLE (1u << 11)
#define MTRR_MASK_VALID (1u << 11)

int mtrr_type_for(uint64_t addr, uint64_t def_type, const uint64_t *bases, const uint64_t *masks,
                  int n, int phys_bits) {
    if (!(def_type & MTRR_DEF_ENABLE)) {
        return MT_UC; /* MTRRs disabled: everything is UC */
    }
    uint64_t phys_mask = (phys_bits >= 64 ? ~0ull : (1ull << phys_bits) - 1) & ~0xfffull;
    int found = -1;
    for (int i = 0; i < n; i++) {
        if (!(masks[i] & MTRR_MASK_VALID)) {
            continue;
        }
        uint64_t m = masks[i] & phys_mask;
        if ((addr & m) != (bases[i] & m)) {
            continue;
        }
        int t = (int)(bases[i] & 0xff);
        if (found < 0 || found == t) {
            found = t;
        } else if (found == MT_UC || t == MT_UC) {
            found = MT_UC; /* UC wins any overlap */
        } else if ((found == MT_WT && t == MT_WB) || (found == MT_WB && t == MT_WT)) {
            found = MT_WT;
        } else {
            return MT_CONFLICT; /* architecturally undefined */
        }
    }
    return found >= 0 ? found : (int)(def_type & 0xff);
}

int pat_type(uint64_t pat_msr, int idx) {
    return (int)((pat_msr >> (8 * (idx & 7))) & 7);
}

int pat_index(int pat, int pcd, int pwt) {
    return (pat ? 4 : 0) | (pcd ? 2 : 0) | (pwt ? 1 : 0);
}

static int row(int mtrr) {
    switch (mtrr) {
    case MT_UC:
        return 0;
    case MT_WC:
        return 1;
    case MT_WT:
        return 2;
    case MT_WB:
        return 3;
    case MT_WP:
        return 4;
    default:
        return -1;
    }
}

static int col(int pat) {
    switch (pat) {
    case MT_UC:
        return 0;
    case MT_UC_MINUS:
        return 1;
    case MT_WC:
        return 2;
    case MT_WT:
        return 3;
    case MT_WP:
        return 4;
    case MT_WB:
        return 5;
    default:
        return -1;
    }
}

int memtype_effective(int mtrr, int pat) {
    /* Rows: MTRR UC, WC, WT, WB, WP. Columns: PAT UC, UC-, WC, WT, WP, WB. */
    // clang-format off
    static const int8_t table[5][6] = {
        /*        UC     UC-    WC     WT     WP     WB  */
        /* UC */ {MT_UC, MT_UC, MT_WC, MT_UC, MT_UC, MT_UC},
        /* WC */ {MT_UC, MT_WC, MT_WC, MT_UC, MT_UC, MT_WC},
        /* WT */ {MT_UC, MT_UC, MT_WC, MT_WT, MT_UC, MT_WT},
        /* WB */ {MT_UC, MT_UC, MT_WC, MT_WT, MT_WP, MT_WB},
        /* WP */ {MT_UC, MT_UC, MT_WC, MT_UC, MT_WP, MT_WP},
    };
    // clang-format on
    if (mtrr == MT_CONFLICT) {
        return MT_CONFLICT;
    }
    int r = row(mtrr), c = col(pat);
    return r < 0 || c < 0 ? -1 : table[r][c];
}

const char *memtype_name(int t) {
    switch (t) {
    case MT_UC:
        return "UC";
    case MT_WC:
        return "WC";
    case MT_WT:
        return "WT";
    case MT_WP:
        return "WP";
    case MT_WB:
        return "WB";
    case MT_UC_MINUS:
        return "UC-";
    case MT_CONFLICT:
        return "conflict";
    default:
        return "?";
    }
}
