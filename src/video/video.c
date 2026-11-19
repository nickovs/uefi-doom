/* GOP driver for the video module. See video.h and docs/architecture.md#video-video. */
#include "video/video.h"

#include <stdlib.h>
#include <string.h>

#include "efi/efi.h"

#include <Protocol/GraphicsOutput.h>

#include "efi/cpu_features.h"
#include "efi/efi_main.h"
#include "efi/log.h"
#include "efi/pat_wc.h"
#include "video/fb_copy.h"

EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
static UINT32 original_mode;
static bool mode_changed, active;
static video_info info;
static pixfmt fmt;
static uint32_t lut[256];
static uint32_t *line;      /* one scaled row */
static uint32_t *backbuf;   /* full scaled image, Blt path only */
static row_copy_fn fb_copy; /* wide stores: the framebuffer may be uncached */
static const char *fb_copy_name = "-";

static EFI_GRAPHICS_OUTPUT_PROTOCOL *find_gop(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *g = NULL;
    if (gST->ConsoleOutHandle &&
        !EFI_ERROR(gBS->HandleProtocol(gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid,
                                       (void **)&g)) &&
        g) {
        return g;
    }
    if (!EFI_ERROR(gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void **)&g))) {
        return g;
    }
    return NULL;
}

void *video_gop(void) {
    if (!gop) {
        gop = find_gop();
    }
    return gop;
}

/* Mode details -> mode_desc + pixfmt. Returns false if the mode is unusable. */
static bool describe(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi, mode_desc *d, pixfmt *pf) {
    d->w = mi->HorizontalResolution;
    d->h = mi->VerticalResolution;
    d->pitch = mi->PixelsPerScanLine;
    d->has_fb = mi->PixelFormat != PixelBltOnly;
    switch (mi->PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor:
        return pixfmt_from_masks(0xff, 0xff00, 0xff0000, pf) == 0;
    case PixelBlueGreenRedReserved8BitPerColor:
    case PixelBltOnly: /* Blt buffers are EFI_GRAPHICS_OUTPUT_BLT_PIXEL: B,G,R,x */
        return pixfmt_from_masks(0xff0000, 0xff00, 0xff, pf) == 0;
    case PixelBitMask:
        return pixfmt_from_masks(mi->PixelInformation.RedMask, mi->PixelInformation.GreenMask,
                                 mi->PixelInformation.BlueMask, pf) == 0;
    default:
        return false;
    }
}

static void restore_cleanup(void *ctx) {
    (void)ctx;
    video_shutdown();
}

static void fill_black(void) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {0};
    gop->Blt(gop, &black, EfiBltVideoFill, 0, 0, 0, 0, info.w, info.h, 0);
}

bool video_init(const video_config *cfg) {
    if (!video_gop()) {
        log_msg(LOG_ERROR, "video: no Graphics Output Protocol");
        return false;
    }
    UINT32 n = gop->Mode->MaxMode;
    mode_desc *modes = calloc(n ? n : 1, sizeof *modes);
    if (!modes) {
        return false;
    }
    for (UINT32 i = 0; i < n; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
        UINTN sz;
        pixfmt pf;
        if (EFI_ERROR(gop->QueryMode(gop, i, &sz, &mi))) {
            continue;
        }
        if (sz >= sizeof *mi && !describe(mi, &modes[i], &pf)) {
            modes[i].w = 0; /* unusable */
        }
        gBS->FreePool(mi);
    }
    mode_choice c;
    int r = video_choose(modes, (int)n, (int)gop->Mode->Mode, cfg->pref_w, cfg->pref_h, VIDEO_SRC_W,
                         VIDEO_SRC_H, cfg->max_scale, cfg->aspect_correct, &c);
    free(modes);
    if (r < 0) {
        log_msg(LOG_ERROR, "video: no usable GOP mode among %u", n);
        return false;
    }

    original_mode = gop->Mode->Mode;
    if ((UINT32)c.mode != original_mode) {
        EFI_STATUS st = gop->SetMode(gop, (UINT32)c.mode);
        if (EFI_ERROR(st)) {
            log_msg(LOG_ERROR, "video: SetMode(%d) failed: %s", c.mode, efi_status_str(st));
            return false;
        }
        mode_changed = true;
    }
    efi_cleanup_push(restore_cleanup, NULL, "video");

    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = gop->Mode->Info;
    mode_desc d;
    if (!describe(mi, &d, &fmt)) {
        log_msg(LOG_ERROR, "video: active mode has an unsupported pixel format");
        return false;
    }
    info.mode = gop->Mode->Mode;
    info.w = d.w;
    info.h = d.h;
    info.pitch = d.pitch;
    info.gop_format = mi->PixelFormat;
    info.fb_base = d.has_fb ? gop->Mode->FrameBufferBase : 0;
    info.fb_size = d.has_fb ? gop->Mode->FrameBufferSize : 0;
    info.k = c.k;
    info.aspect = c.aspect;
    info.x0 = c.x0;
    info.y0 = c.y0;
    info.out_w = VIDEO_SRC_W * c.k;
    info.out_h = scale_out_height(VIDEO_SRC_H, c.k, c.aspect);
    info.use_blt = cfg->force_blt || !d.has_fb;
    if (!info.use_blt &&
        info.fb_size < (uint64_t)info.pitch * info.h * 4) { /* validate before writing to it */
        log_msg(LOG_WARN, "video: framebuffer too small for mode (0x%llx); using Blt",
                (unsigned long long)info.fb_size);
        info.use_blt = true;
    }

    line = malloc((size_t)info.out_w * 4);
    backbuf = info.use_blt ? malloc((size_t)info.out_w * info.out_h * 4) : NULL;
    if (!line || (info.use_blt && !backbuf)) {
        log_msg(LOG_ERROR, "video: out of memory");
        return false;
    }
    if (!info.use_blt) {
        if (cfg->allow_wc) {
            /* ~100x faster than the uncached default on real hardware; restored at exit */
            char why[96] = "";
            info.wc = pat_wc_enable(info.fb_base, info.fb_size, why, sizeof why) == 0;
            if (!info.wc) {
                log_msg(LOG_INFO, "video: write-combining unavailable (%s); uncached writes", why);
            }
        }
        if (cfg->allow_avx) {
            cpu_avx_enable(); /* 2x throughput on uncached memory; restored at exit */
        }
        fb_copy = fb_copy_best(&fb_copy_name);
    }
    log_msg(LOG_INFO,
            "video: mode %u %ux%u pitch %u fmt %d fb 0x%llx; scale %u%s at (%u,%u) via %s (%s%s)",
            info.mode, info.w, info.h, info.pitch, info.gop_format,
            (unsigned long long)info.fb_base, info.k, info.aspect ? " 4:3" : "", info.x0, info.y0,
            info.use_blt ? "Blt" : "framebuffer", fb_copy_name, info.wc ? ", write-combining" : "");

    log_set_conout(false); /* ConOut would draw over the image */
    gST->ConOut->EnableCursor(gST->ConOut, FALSE);
    fill_black();
    active = true;
    return true;
}

void video_present(const uint8_t *frame, const uint8_t rgb[256][3], bool palette_changed) {
    if (!active) {
        return;
    }
    static bool lut_built;
    if (palette_changed || !lut_built) {
        palette_build_lut(rgb, &fmt, lut);
        lut_built = true;
    }
    if (info.use_blt) {
        scale_frame(frame, VIDEO_SRC_W, VIDEO_SRC_H, lut, backbuf, info.out_w, info.k, info.aspect,
                    line, NULL);
        gop->Blt(gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)backbuf, EfiBltBufferToVideo, 0, 0, info.x0,
                 info.y0, info.out_w, info.out_h, 0);
    } else {
        uint32_t *fb = (uint32_t *)(uintptr_t)info.fb_base + (size_t)info.y0 * info.pitch + info.x0;
        scale_frame(frame, VIDEO_SRC_W, VIDEO_SRC_H, lut, fb, info.pitch, info.k, info.aspect, line,
                    fb_copy);
        if (info.wc) {
            __asm__ volatile("sfence" ::: "memory"); /* drain write-combining buffers */
        }
    }
}

void video_shutdown(void) {
    if (!gop) {
        return;
    }
    if (mode_changed) {
        gop->SetMode(gop, original_mode);
        mode_changed = false;
    } else if (active) {
        fill_black();
    }
    if (info.wc) {
        pat_wc_restore();
        info.wc = false;
    }
    active = false;
    free(line);
    free(backbuf);
    line = backbuf = NULL;
    gST->ConOut->ClearScreen(gST->ConOut);
    gST->ConOut->EnableCursor(gST->ConOut, TRUE);
    log_set_conout(true);
}

const video_info *video_get_info(void) {
    return &info;
}
