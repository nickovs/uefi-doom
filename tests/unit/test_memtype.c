#include "check.h"
#include "efi/memtype_core.h"

int main(void) {
    const uint64_t def_wb = (1u << 11) | MT_WB; /* enabled, default WB */
    /* 2 GiB UC hole at 2..4 GiB (typical MMIO), and a WC range for a framebuffer. */
    uint64_t bases[] = {0x80000000ull | MT_UC, 0x4000000000ull | MT_WC};
    uint64_t masks[] = {0x7f80000000ull | (1u << 11), 0x7ff0000000ull | (1u << 11)};

    CHECK_EQ(mtrr_type_for(0x10000000, def_wb, bases, masks, 2, 39), MT_WB);
    CHECK_EQ(mtrr_type_for(0xc0000000, def_wb, bases, masks, 2, 39), MT_UC);
    CHECK_EQ(mtrr_type_for(0x4008000000ull, def_wb, bases, masks, 2, 39), MT_WC);
    CHECK_EQ(mtrr_type_for(0xc0000000, MT_WB /* E clear */, bases, masks, 2, 39), MT_UC);

    /* Invalid (valid bit clear) ranges are ignored. */
    uint64_t masks_off[] = {0x7f80000000ull, 0x7ff0000000ull};
    CHECK_EQ(mtrr_type_for(0xc0000000, def_wb, bases, masks_off, 2, 39), MT_WB);

    /* Overlaps: UC wins; WT+WB -> WT; WC+WB -> conflict. */
    uint64_t ob[] = {0x0 | MT_WB, 0x0 | MT_UC};
    uint64_t om[] = {0x7f00000000ull | (1u << 11), 0x7f00000000ull | (1u << 11)};
    CHECK_EQ(mtrr_type_for(0x1000, def_wb, ob, om, 2, 39), MT_UC);
    ob[1] = MT_WT;
    CHECK_EQ(mtrr_type_for(0x1000, def_wb, ob, om, 2, 39), MT_WT);
    ob[1] = MT_WC;
    CHECK_EQ(mtrr_type_for(0x1000, def_wb, ob, om, 2, 39), MT_CONFLICT);

    /* Power-on default PAT: WB, WT, UC-, UC repeated. */
    uint64_t pat = 0x0007040600070406ull;
    CHECK_EQ(pat_type(pat, 0), MT_WB);
    CHECK_EQ(pat_type(pat, 1), MT_WT);
    CHECK_EQ(pat_type(pat, 2), MT_UC_MINUS);
    CHECK_EQ(pat_type(pat, 3), MT_UC);
    CHECK_EQ(pat_index(0, 1, 1), 3);
    CHECK_EQ(pat_index(1, 0, 0), 4);

    CHECK_EQ(memtype_effective(MT_UC, MT_WB), MT_UC);
    CHECK_EQ(memtype_effective(MT_WC, MT_WB), MT_WC);
    CHECK_EQ(memtype_effective(MT_WC, MT_UC_MINUS), MT_WC);
    CHECK_EQ(memtype_effective(MT_UC, MT_UC_MINUS), MT_UC);
    CHECK_EQ(memtype_effective(MT_UC, MT_WC), MT_WC); /* PAT WC overrides MTRR UC */
    CHECK_EQ(memtype_effective(MT_WB, MT_WB), MT_WB);
    CHECK_EQ(memtype_effective(MT_CONFLICT, MT_WB), MT_CONFLICT);
    CHECK_STR(memtype_name(MT_UC_MINUS), "UC-");

    return CHECK_DONE();
}
