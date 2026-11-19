/* HWPROBE video section: GOP modes, framebuffer memory type, write speeds.
 * Answers "how large a scale can we afford?" (analysis section 4, design 06). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efi/efi.h"

#include <Guid/DxeServices.h>
#include <Pi/PiDxeCis.h>
#include <Protocol/GraphicsOutput.h>

#include "efi/clock.h"
#include "efi/cpu.h"
#include "efi/cpu_features.h"
#include "efi/efi_main.h"
#include "efi/log.h"
#include "efi/memtype_core.h"
#include "efi/pat_wc.h"
#include "hwprobe.h"
#include "video/fb_copy.h"
#include "video/video.h"

EFI_GUID gEfiDxeServicesTableGuid = DXE_SERVICES_TABLE_GUID;

#define MSR_MTRRCAP 0xfe
#define MSR_MTRR_DEF_TYPE 0x2ff
#define MSR_MTRR_PHYSBASE0 0x200
#define MSR_PAT 0x277
#define TICK_BUDGET_US 14300 /* half of a 35 Hz tic */

typedef void (*fill_fn)(void);

static const video_info *vi;
static uint32_t *fb, *ram, *line;
static uint8_t *frame;
static uint32_t lut[256];

/* ---- memory type ------------------------------------------------------------ */

static void report_memtype(uint64_t addr) {
    uint32_t r[4];
    cpuid(0x80000008, 0, r);
    int phys_bits = (int)(r[0] & 0xff);
    uint64_t cap = rdmsr(MSR_MTRRCAP), def = rdmsr(MSR_MTRR_DEF_TYPE);
    int n = (int)(cap & 0xff);
    n = n > 32 ? 32 : n;
    uint64_t bases[32], masks[32];
    for (int i = 0; i < n; i++) {
        bases[i] = rdmsr(MSR_MTRR_PHYSBASE0 + 2 * (uint32_t)i);
        masks[i] = rdmsr(MSR_MTRR_PHYSBASE0 + 2 * (uint32_t)i + 1);
    }
    int mtrr = mtrr_type_for(addr, def, bases, masks, n, phys_bits);
    uint64_t pat = rdmsr(MSR_PAT);

    /* Walk the 4-level page tables (identity-mapped under boot services). */
    int pat_t = -1, level = 0;
    if (read_cr4() & (1u << 12)) {
        log_msg(LOG_INFO, "5-level paging: page walk skipped");
    } else {
        uint64_t *tbl = (uint64_t *)(uintptr_t)(read_cr3() & 0x000ffffffffff000ull);
        for (level = 4; level >= 1; level--) {
            uint64_t e = tbl[(addr >> (12 + 9 * (level - 1))) & 511];
            if (!(e & 1)) {
                level = 0; /* not present */
                break;
            }
            bool leaf = level == 1 || (level <= 3 && (e & (1u << 7)));
            if (leaf) {
                int pat_bit = level == 1 ? (e >> 7) & 1 : (e >> 12) & 1;
                pat_t = pat_type(pat, pat_index(pat_bit, (e >> 4) & 1, (e >> 3) & 1));
                break;
            }
            tbl = (uint64_t *)(uintptr_t)(e & 0x000ffffffffff000ull);
        }
    }
    int eff = pat_t >= 0 ? memtype_effective(mtrr, pat_t) : mtrr;
    log_msg(LOG_INFO, "framebuffer memory type: MTRR %s, PAT %s (%s page) -> effective %s",
            memtype_name(mtrr), pat_t >= 0 ? memtype_name(pat_t) : "?",
            level == 3   ? "1G"
            : level == 2 ? "2M"
            : level == 1 ? "4K"
                         : "no",
            memtype_name(eff));
    rep_obj("memtype");
    rep_hex("mtrr_def_type", def);
    rep_int("variable_mtrrs", n);
    rep_str("mtrr", memtype_name(mtrr));
    rep_hex("pat_msr", pat);
    rep_str("pat", pat_t >= 0 ? memtype_name(pat_t) : "?");
    rep_str("effective", memtype_name(eff));
    rep_end();
}

/* ---- timed operations ----------------------------------------------------------- */

static void fill32(void) {
    for (uint32_t y = 0; y < vi->h; y++) {
        volatile uint32_t *p = fb + (size_t)y * vi->pitch;
        for (uint32_t x = 0; x < vi->w; x++) {
            p[x] = x ^ y;
        }
    }
}

typedef uint32_t v4u __attribute__((vector_size(16)));

static void fill256(void) {
    for (uint32_t y = 0; y < vi->h; y++) {
        uint32_t *p = fb + (size_t)y * vi->pitch;
        for (uint32_t x = 0; x + 8 <= vi->w; x += 8) {
            __asm__ volatile("vpcmpeqd %%ymm0, %%ymm0, %%ymm0\n\tvmovdqa %%ymm0, (%0)"
                             :
                             : "r"(p + x)
                             : "xmm0", "memory");
        }
    }
    __asm__ volatile("vzeroupper" ::: "memory");
}

static void fill128(void) {
    v4u v = {1, 2, 3, 4};
    for (uint32_t y = 0; y < vi->h; y++) {
        volatile v4u *p = (volatile v4u *)(fb + (size_t)y * vi->pitch);
        for (uint32_t x = 0; x < vi->w / 4; x++) {
            p[x] = v;
        }
    }
}

static void copy_rep_movsb(void) {
    for (uint32_t y = 0; y < vi->h; y++) {
        memcpy(fb + (size_t)y * vi->pitch, ram + (size_t)y * vi->w, (size_t)vi->w * 4);
    }
}

static void blt_full(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = video_gop();
    gop->Blt(gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)ram, EfiBltBufferToVideo, 0, 0, 0, 0, vi->w,
             vi->h, 0);
}

static volatile uint32_t sink;
static void read_back(void) {
    uint32_t s = 0;
    for (uint32_t y = 0; y < vi->h; y++) {
        volatile uint32_t *p = fb + (size_t)y * vi->pitch;
        for (uint32_t x = 0; x < vi->w; x++) {
            s += p[x];
        }
    }
    sink = s;
}

static unsigned cur_k;
static bool cur_aspect;
static row_copy_fn cur_copy;
static void scale_path(void) {
    uint32_t oh = scale_out_height(VIDEO_SRC_H, cur_k, cur_aspect);
    uint32_t x0 = (vi->w - VIDEO_SRC_W * cur_k) / 2, y0 = (vi->h - oh) / 2;
    scale_frame(frame, VIDEO_SRC_W, VIDEO_SRC_H, lut, fb + (size_t)y0 * vi->pitch + x0, vi->pitch,
                cur_k, cur_aspect, line, cur_copy);
}

/* Median microseconds for fn; fewer repetitions when one run is slow. */
static uint64_t time_us(fill_fn fn) {
    uint64_t v[20];
    uint64_t t0 = clock_us();
    fn();
    v[0] = clock_us() - t0;
    int n = v[0] > 200000 ? 1 : v[0] > 50000 ? 5 : 20;
    for (int i = 1; i < n; i++) {
        t0 = clock_us();
        fn();
        v[i] = clock_us() - t0;
    }
    return hw_median(v, n);
}

static void timed(const char *name, fill_fn fn, uint64_t bytes) {
    uint64_t us = time_us(fn);
    double mbs = us ? (double)bytes / (double)us : 0; /* bytes/us == MB/s */
    log_msg(LOG_INFO, "  %-22s %8llu us  %8.1f MB/s", name, (unsigned long long)us, mbs);
    rep_obj(NULL);
    rep_str("test", name);
    rep_int("us", (int64_t)us);
    rep_num("mb_per_s", mbs);
    rep_end();
}

static void run_timings(const char *label) {
    uint64_t screen = (uint64_t)vi->w * vi->h * 4;
    log_msg(LOG_INFO, "timings (%s), %ux%u = %llu bytes per full screen:", label, vi->w, vi->h,
            (unsigned long long)screen);
    rep_arr(label);
    timed("fill_32bit", fill32, screen);
    if (vi->pitch % 4 == 0 && ((uintptr_t)fb & 15) == 0) {
        timed("fill_128bit_sse", fill128, screen);
    }
    bool avx = fb_avx_usable();
    if (avx && ((uintptr_t)fb & 31) == 0) {
        timed("fill_256bit_avx", fill256, screen);
    }
    timed("copy_rep_movsb", copy_rep_movsb, screen);
    timed("gop_blt", blt_full, screen);
    static const struct {
        const char *name;
        row_copy_fn fn;
    } copies[] = {{"sse", fb_copy_sse}, {"avx", fb_copy_avx}};
    unsigned kmax = vi->w / VIDEO_SRC_W;
    unsigned best_k = 0;
    for (unsigned k = 1; k <= kmax && k <= 8; k++) {
        for (int c = 0; c < (avx ? 2 : 1); c++) {
            int a = 1; /* 4:3 corrected: the default and the larger case */
            cur_k = k;
            cur_aspect = a;
            cur_copy = copies[c].fn;
            uint32_t oh = scale_out_height(VIDEO_SRC_H, k, a);
            if (oh > vi->h) {
                continue;
            }
            char name[32];
            snprintf(name, sizeof name, "doom_scale_%u_4x3_%s", k, copies[c].name);
            uint64_t bytes = (uint64_t)VIDEO_SRC_W * k * oh * 4;
            uint64_t us = time_us(scale_path);
            log_msg(LOG_INFO, "  %-22s %8llu us  %8.1f MB/s", name, (unsigned long long)us,
                    us ? (double)bytes / (double)us : 0.0);
            rep_obj(NULL);
            rep_str("test", name);
            rep_int("us", (int64_t)us);
            rep_num("mb_per_s", us ? (double)bytes / (double)us : 0.0);
            rep_end();
            if (a && us <= TICK_BUDGET_US) {
                best_k = k;
            }
        }
    }
    timed("read_back", read_back, screen);
    rep_end();
    log_msg(LOG_INFO, "largest 4:3 scale within half a tic (%u us): %u", TICK_BUDGET_US, best_k);
    rep_int("suggested_max_scale", best_k);
}

/* ---- AVX (-avx) ------------------------------------------------------------------ */

static void avx_enable(void) {
    static const char *const names[] = {"unavailable", "already_on", "enabled"};
    avx_state st = cpu_avx_enable();
    uint64_t cr4, xcr0;
    cpu_avx_saved(&cr4, &xcr0);
    rep_obj("avx_experiment");
    rep_str("state", names[st]);
    rep_hex("cr4_before", cr4);
    rep_hex("xcr0_before", xcr0);
    rep_bool("enabled", fb_avx_usable());
    rep_end();
}

/* ---- write-combining experiment (interactive only) ----------------------------- */

static EFI_DXE_SERVICES *dxe(void) {
    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        if (memcmp(&gST->ConfigurationTable[i].VendorGuid, &gEfiDxeServicesTableGuid,
                   sizeof(EFI_GUID)) == 0) {
            return gST->ConfigurationTable[i].VendorTable;
        }
    }
    return NULL;
}

static uint64_t wc_saved_attrs;
static bool wc_changed;

static void wc_restore(void *ctx) {
    (void)ctx;
    if (wc_changed) {
        EFI_STATUS st = dxe()->SetMemorySpaceAttributes(vi->fb_base, vi->fb_size, wc_saved_attrs);
        log_msg(LOG_INFO, "WC experiment: attributes restored (%s)", efi_status_str(st));
        wc_changed = false;
    }
}

static void wc_experiment(void) {
    EFI_DXE_SERVICES *ds = dxe();
    EFI_GCD_MEMORY_SPACE_DESCRIPTOR d;
    if (!ds || EFI_ERROR(ds->GetMemorySpaceDescriptor(vi->fb_base, &d))) {
        log_msg(LOG_INFO, "WC experiment: no DXE services / descriptor");
        return;
    }
    rep_obj("wc_experiment");
    rep_bool("avx_usable", fb_avx_usable());
    rep_hex("attributes", d.Attributes);
    rep_hex("capabilities", d.Capabilities);
    if (!(d.Capabilities & EFI_MEMORY_WC) || d.BaseAddress + d.Length < vi->fb_base + vi->fb_size) {
        log_msg(LOG_INFO, "WC experiment: not supported for this range (caps 0x%llx)",
                (unsigned long long)d.Capabilities);
        rep_end();
        return;
    }
    if (!hw_wc) {
        rep_end();
        return;
    }
    const uint64_t cache_bits = EFI_MEMORY_UC | EFI_MEMORY_WC | EFI_MEMORY_WT | EFI_MEMORY_WB |
                                EFI_MEMORY_UCE | EFI_MEMORY_WP;
    wc_saved_attrs = d.Attributes;
    efi_cleanup_push(wc_restore, NULL, "wc experiment");
    EFI_STATUS st = ds->SetMemorySpaceAttributes(vi->fb_base, vi->fb_size,
                                                 (d.Attributes & ~cache_bits) | EFI_MEMORY_WC);
    log_msg(LOG_INFO, "WC experiment: SetMemorySpaceAttributes: %s", efi_status_str(st));
    rep_str("set_status", efi_status_str(st));
    if (!EFI_ERROR(st)) {
        wc_changed = true;
        report_memtype(vi->fb_base);
        run_timings("timings_wc");
        wc_restore(NULL);
    }
    rep_end();
}

/* The port's own path: video_init with write-combining and AVX allowed, then
 * video_present of a full frame. Interactive runs also check it on screen. */
static void port_path(void) {
    video_config pc = {.aspect_correct = true, .allow_wc = true, .allow_avx = true};
    bool show = hw_wait_enter("visual check. After ENTER, a test pattern shows for 5 s: a white "
                              "border around diagonal colour stripes.",
                              120000);
    uint8_t *f = malloc(VIDEO_SRC_W * VIDEO_SRC_H);
    static uint8_t pal[256][3];
    if (!f || !video_init(&pc)) {
        free(f);
        return;
    }
    for (int i = 0; i < 256; i++) {
        pal[i][0] = (uint8_t)i;
        pal[i][1] = (uint8_t)(255 - i);
        pal[i][2] = (uint8_t)(i * 4);
    }
    for (int y = 0; y < VIDEO_SRC_H; y++) {
        for (int x = 0; x < VIDEO_SRC_W; x++) {
            /* diagonal colour bands with a white border so the frame edge is visible */
            bool edge = x < 2 || y < 2 || x >= VIDEO_SRC_W - 2 || y >= VIDEO_SRC_H - 2;
            f[y * VIDEO_SRC_W + x] = edge ? 255 : (uint8_t)((x + y) & 0xfc);
        }
    }
    const video_info *pi = video_get_info();
    uint64_t v[20];
    for (int i = 0; i < 20; i++) {
        uint64_t t0 = clock_us();
        video_present(f, pal, i == 0);
        v[i] = clock_us() - t0;
    }
    uint64_t med = hw_median(v, 20);
    bool wc = pi->wc, avx = fb_avx_usable();
    rep_obj("port_present");
    rep_int("scale", pi->k);
    rep_bool("aspect", pi->aspect);
    rep_bool("write_combining", wc);
    rep_bool("avx", avx);
    rep_int("median_us", (int64_t)med);
    if (show) {
        clock_sleep_us(5000000); /* leave the pattern up to be seen */
    }
    video_shutdown();
    cpu_avx_restore();
    log_msg(LOG_INFO, "port present path: %ux%u at %u%s, %s, %s: median %llu us per frame",
            pi->out_w, pi->out_h, pi->k, pi->aspect ? " 4:3" : "",
            wc ? "write-combining" : "uncached", avx ? "avx" : "sse", (unsigned long long)med);
    if (show) {
        rep_bool("pattern_seen",
                 hw_ask("Did you see the white border and diagonal colour stripes?", 60000));
    }
    rep_end();
    free(f);
}

void probe_video(void) {
    log_msg(LOG_INFO, "== video");
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = video_gop();
    rep_obj("video");
    if (!gop) {
        log_msg(LOG_WARN, "no GOP");
        rep_bool("gop", false);
        rep_end();
        return;
    }
    rep_int("current_mode", gop->Mode->Mode);
    rep_arr("modes");
    for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
        UINTN sz;
        if (EFI_ERROR(gop->QueryMode(gop, i, &sz, &mi))) {
            continue;
        }
        log_msg(LOG_INFO, "mode %u: %ux%u pitch %u format %d%s", i, mi->HorizontalResolution,
                mi->VerticalResolution, mi->PixelsPerScanLine, mi->PixelFormat,
                i == gop->Mode->Mode ? " (current)" : "");
        rep_obj(NULL);
        rep_int("mode", i);
        rep_int("w", mi->HorizontalResolution);
        rep_int("h", mi->VerticalResolution);
        rep_int("pitch", mi->PixelsPerScanLine);
        rep_int("format", mi->PixelFormat);
        rep_end();
        gBS->FreePool(mi);
    }
    rep_end();

    video_config cfg = {.aspect_correct = true};
    if (!video_init(&cfg)) {
        rep_end();
        return;
    }
    vi = video_get_info();
    rep_hex("fb_base", vi->fb_base);
    rep_hex("fb_size", vi->fb_size);
    if (!vi->fb_base) {
        log_msg(LOG_INFO, "Blt-only mode: framebuffer tests skipped");
        video_shutdown();
        rep_end();
        return;
    }
    fb = (uint32_t *)(uintptr_t)vi->fb_base;
    ram = malloc((size_t)vi->w * vi->h * 4);
    line = malloc((size_t)vi->w * 4);
    frame = malloc(VIDEO_SRC_W * VIDEO_SRC_H);
    if (!ram || !line || !frame) {
        log_msg(LOG_WARN, "out of memory for video tests");
        video_shutdown();
        rep_end();
        return;
    }
    for (size_t i = 0; i < (size_t)VIDEO_SRC_W * VIDEO_SRC_H; i++) {
        frame[i] = (uint8_t)(i * 7 + i / VIDEO_SRC_W);
    }
    for (size_t i = 0; i < (size_t)vi->w * vi->h; i++) {
        ram[i] = (uint32_t)i * 2654435761u;
    }
    for (int i = 0; i < 256; i++) {
        lut[i] = (uint32_t)i * 0x010101u;
    }

    report_memtype(vi->fb_base);
    if (hw_avx) {
        avx_enable();
    }
    run_timings("timings");
    rep_bool("avx_usable", fb_avx_usable());
    if (hw_wc) {
        wc_experiment();
    }
    if (hw_patwc) {
        log_msg(LOG_INFO, "PAT WC experiment:");
        rep_obj("patwc_experiment");
        char why[96] = "";
        int r = pat_wc_enable(vi->fb_base, vi->fb_size, why, sizeof why);
        rep_bool("enabled", r == 0);
        if (r == 0) {
            report_memtype(vi->fb_base);
            run_timings("timings_patwc");
            pat_wc_restore();
            report_memtype(vi->fb_base); /* must be back to the original type */
        } else {
            log_msg(LOG_INFO, "PAT WC experiment: not done: %s", why);
            rep_str("reason", why);
        }
        rep_end();
    }
    cpu_avx_restore();
    video_shutdown(); /* restores ConOut; logs printed during the tests show again */
    free(ram);
    free(line);
    free(frame);
    port_path();
    rep_end();
}
