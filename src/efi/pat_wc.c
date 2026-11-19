/* PAT-based write-combining. See pat_wc.h and Intel SDM vol. 3, "Page Attribute
 * Table" (programming the PAT, and the MTRR x PAT combination rules). */
#include "efi/pat_wc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "efi/alloc.h"
#include "efi/cpu.h"
#include "efi/efi_main.h"
#include "efi/log.h"
#include "efi/memtype_core.h"

#define MSR_PAT 0x277
#define PTE_P (1ull << 0)
#define PTE_PWT (1ull << 3)
#define PTE_PCD (1ull << 4)
#define PTE_PS (1ull << 7)
#define PTE_PAT_LARGE (1ull << 12) /* PAT bit in 2 MiB / 1 GiB leaf entries */
#define PTE_PAT_4K (1ull << 7)
#define ADDR_MASK 0x000ffffffffff000ull
#define ADDR_1G 0x000fffffc0000000ull
#define ADDR_2M 0x000fffffffe00000ull
#define FLAGS_LARGE_KEEP (0xfffull | PTE_PAT_LARGE | 0xfff0000000000000ull)
#define SZ_2M (2ull << 20)
#define SZ_1G (1ull << 30)
#define CR0_WP (1ull << 16)
#define CR0_CD (1ull << 30)
#define CR0_NW (1ull << 29)
#define MAX_2M 64 /* up to 128 MiB of 2 MiB entries changed in place */

static bool active;
static uint64_t saved_pat;
static bool pat_written;
static volatile uint64_t *pdpt_slot; /* split case: the PDPT entry we replaced */
static uint64_t saved_pdpt;
static volatile uint64_t *pd_slots[MAX_2M]; /* 2 MiB case: entries changed in place */
static uint64_t saved_pd[MAX_2M];
static int n_pd;

static inline uint64_t read_cr0(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline void write_cr0(uint64_t v) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory");
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

static inline void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

static inline uint64_t irq_save(void) {
    uint64_t f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) : : "memory");
    return f;
}

static inline void irq_restore(uint64_t f) {
    if (f & (1u << 9)) {
        __asm__ volatile("sti" ::: "memory");
    }
}

static void flush_tlb(void) {
    write_cr3(read_cr3());
}

/* SDM: change the PAT with caches disabled and flushed on both sides. */
static void set_pat(uint64_t v) {
    uint64_t f = irq_save();
    uint64_t cr0 = read_cr0();
    write_cr0((cr0 | CR0_CD) & ~CR0_NW);
    __asm__ volatile("wbinvd" ::: "memory");
    wrmsr(MSR_PAT, v);
    flush_tlb();
    __asm__ volatile("wbinvd" ::: "memory");
    write_cr0(cr0);
    irq_restore(f);
}

/* Firmware may map its page tables read-only; lift CR0.WP around our writes. */
static void pt_write(volatile uint64_t *slot, uint64_t v) {
    uint64_t f = irq_save();
    uint64_t cr0 = read_cr0();
    write_cr0(cr0 & ~CR0_WP);
    *slot = v;
    write_cr0(cr0);
    irq_restore(f);
}

static uint64_t *table(uint64_t entry) {
    return (uint64_t *)(uintptr_t)(entry & ADDR_MASK);
}

/* Mark which PAT indexes any present leaf mapping uses. */
static void scan_used(uint64_t *t, int level, bool used[8]) {
    for (int i = 0; i < 512; i++) {
        uint64_t e = t[i];
        if (!(e & PTE_P)) {
            continue;
        }
        bool leaf = level == 1 || (level <= 3 && (e & PTE_PS));
        if (leaf) {
            int pat = level == 1 ? (e & PTE_PAT_4K) != 0 : (e & PTE_PAT_LARGE) != 0;
            used[pat_index(pat, (e & PTE_PCD) != 0, (e & PTE_PWT) != 0)] = true;
        } else if (level > 1) {
            scan_used(table(e), level - 1, used);
        }
    }
}

static uint64_t with_index(uint64_t e, int idx) {
    e &= ~(PTE_PAT_LARGE | PTE_PCD | PTE_PWT);
    return e | (idx & 4 ? PTE_PAT_LARGE : 0) | (idx & 2 ? PTE_PCD : 0) | (idx & 1 ? PTE_PWT : 0);
}

void pat_wc_restore(void) {
    if (!active) {
        return;
    }
    if (pdpt_slot) {
        pt_write(pdpt_slot, saved_pdpt);
        pdpt_slot = NULL;
    }
    for (int i = 0; i < n_pd; i++) {
        pt_write(pd_slots[i], saved_pd[i]);
    }
    n_pd = 0;
    flush_tlb();
    if (pat_written) {
        set_pat(saved_pat);
        pat_written = false;
    }
    active = false;
    log_msg(LOG_INFO, "pat_wc: page tables and PAT restored");
}

static void restore_cleanup(void *ctx) {
    (void)ctx;
    pat_wc_restore();
}

int pat_wc_enable(uint64_t base, uint64_t size, char *why, size_t why_len) {
    uint32_t r[4];
    cpuid(1, 0, r);
    if (!((r[3] >> 16) & 1)) {
        snprintf(why, why_len, "CPU has no PAT");
        return -1;
    }
    if (read_cr4() & (1u << 12)) {
        snprintf(why, why_len, "5-level paging not supported");
        return -1;
    }
    if (active) {
        snprintf(why, why_len, "already active");
        return -1;
    }
    uint64_t start = base & ~(SZ_2M - 1);
    uint64_t end = (base + size + SZ_2M - 1) & ~(SZ_2M - 1);
    if (start / SZ_1G != (end - 1) / SZ_1G) {
        snprintf(why, why_len, "range crosses a 1 GiB boundary");
        return -1;
    }

    /* Choose a PAT index: one already WC, else an unused upper entry to reprogram. */
    saved_pat = rdmsr(MSR_PAT);
    int idx = -1;
    for (int i = 0; i < 8 && idx < 0; i++) {
        if (pat_type(saved_pat, i) == MT_WC) {
            idx = i;
        }
    }
    bool used[8] = {false};
    uint64_t *pml4 = table(read_cr3());
    scan_used(pml4, 4, used);
    for (int i = 7; i >= 4 && idx < 0; i--) {
        if (!used[i]) {
            idx = i;
        }
    }
    if (idx < 0) {
        snprintf(why, why_len, "no free PAT entry (all of 4-7 in use)");
        return -1;
    }

    /* Find the entry mapping the range. */
    uint64_t e4 = pml4[(start >> 39) & 511];
    if (!(e4 & PTE_P)) {
        snprintf(why, why_len, "range not mapped (PML4)");
        return -1;
    }
    uint64_t *pdpt = table(e4);
    volatile uint64_t *s3 = &pdpt[(start >> 30) & 511];
    uint64_t e3 = *s3;
    if (!(e3 & PTE_P)) {
        snprintf(why, why_len, "range not mapped (PDPT)");
        return -1;
    }

    /* Allocate before registering the restore cleanup: cleanups run LIFO, so the
     * page directory must be freed only after the original entry is back. */
    uint64_t *pd = NULL;
    if (e3 & PTE_PS) {
        pd = alloc_pages(4096);
        if (!pd) {
            snprintf(why, why_len, "out of memory for a page directory");
            return -1;
        }
    }
    efi_cleanup_push(restore_cleanup, NULL, "pat_wc");
    active = true;
    if (e3 & PTE_PS) {
        /* 1 GiB leaf: build a page directory of 2 MiB pages with identical
         * attributes, then give only our range the WC index. */
        uint64_t gbase = e3 & ADDR_1G;
        uint64_t flags = e3 & FLAGS_LARGE_KEEP;
        for (uint64_t i = 0; i < 512; i++) {
            uint64_t a = gbase + i * SZ_2M;
            uint64_t e = flags | a;
            pd[i] = a >= start && a < end ? with_index(e, idx) : e;
        }
        saved_pdpt = e3;
        pdpt_slot = s3;
        /* Non-leaf entry: present, writable, user (as before); PWT/PCD clear. */
        uint64_t nonleaf = (uint64_t)(uintptr_t)pd | (e3 & 0x7) | (1ull << 5);
        pt_write(s3, nonleaf);
    } else {
        pd = table(e3);
        for (uint64_t a = start; a < end; a += SZ_2M) {
            volatile uint64_t *s2 = &pd[(a >> 21) & 511];
            uint64_t e2 = *s2;
            if (!(e2 & PTE_P) || !(e2 & PTE_PS) || n_pd >= MAX_2M) {
                pat_wc_restore();
                snprintf(why, why_len, "range is mapped with 4 KiB pages (not supported)");
                return -1;
            }
            pd_slots[n_pd] = s2;
            saved_pd[n_pd++] = e2;
            pt_write(s2, with_index(e2, idx));
        }
    }
    if (pat_type(saved_pat, idx) != MT_WC) {
        uint64_t pat = (saved_pat & ~(0xffull << (8 * idx))) | ((uint64_t)MT_WC << (8 * idx));
        set_pat(pat);
        pat_written = true;
    } else {
        flush_tlb();
    }
    /* shortcut: the range is widened to whole 2 MiB pages; fine for a GPU
     * aperture (Linux maps the whole aperture WC), revisit for other devices. */
    log_msg(LOG_INFO, "pat_wc: 0x%llx-0x%llx WC via PAT[%d] (%s; PAT 0x%016llx -> 0x%016llx)",
            (unsigned long long)start, (unsigned long long)end, idx,
            pdpt_slot ? "1G page split into 2M pages" : "2M entries updated",
            (unsigned long long)saved_pat, (unsigned long long)rdmsr(MSR_PAT));
    return 0;
}
