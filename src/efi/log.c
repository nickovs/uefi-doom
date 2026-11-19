/* Logging sinks: RAM ring (always), ConOut (until graphics start), QEMU
 * debugcon (DEBUGCON=1 builds only), a log file in the image directory (at
 * exit, and every N seconds with -logsync N), DoomLog variables at exit
 * (devloop). */
#include "efi/log.h"

#include <stdio.h>
#include <string.h>

#include "efi/cpu.h"
#include "efi/dlog.h"
#include "efi/efi.h"
#include "efi/efi_main.h"
#include "efi/fs.h"
#include "efi/ring_core.h"

#define LOG_RING_SIZE (256u * 1024)
#define LOG_HEAD_SIZE (16u * 1024)

static uint8_t ring_buf[LOG_RING_SIZE];
static log_ring ring;
static bool conout_on;
static bool at_line_start = true;

static char file_path[40]; /* empty: file sink off */
static EFI_EVENT sync_event;
static uint64_t synced_total;   /* ring.total at the last file write */
static volatile bool file_busy; /* set while writing; log_write drops input */

static void file_write_snapshot(void) {
    if (!file_path[0] || file_busy) {
        return;
    }
    file_busy = true;
    int err;
    fs_file *f = fs_open(file_path, FS_WRITE, &err);
    if (f) {
        log_span spans[3];
        uint64_t dropped;
        size_t n = ring_snapshot(&ring, ring.cap, spans, &dropped);
        for (size_t i = 0; i < n; i++) {
            if (i == 1 && dropped) {
                char marker[64];
                snprintf(marker, sizeof marker, "\n[... %llu bytes dropped ...]\n",
                         (unsigned long long)dropped);
                fs_write(f, marker, strlen(marker));
            }
            fs_write(f, spans[i].p, spans[i].n);
        }
        fs_close(f);
    }
    synced_total = ring.total;
    file_busy = false;
}

/* Periodic timer at TPL_CALLBACK: still fires while the main code spins at
 * TPL_APPLICATION (e.g. a hang), and file I/O is permitted at this TPL. */
static void EFIAPI sync_tick(EFI_EVENT ev, void *ctx) {
    (void)ev;
    (void)ctx;
    if (ring.total != synced_total) {
        file_write_snapshot();
    }
}

static void file_cleanup(void *ctx) {
    (void)ctx;
    if (sync_event) {
        gBS->CloseEvent(sync_event);
        sync_event = NULL;
    }
    file_write_snapshot();
}

void log_file_enable(const char *path, uint32_t sync_secs) {
    snprintf(file_path, sizeof file_path, "%s", path);
    efi_cleanup_push(file_cleanup, NULL, "log file");
    if (sync_secs) {
        EFI_STATUS st = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, sync_tick,
                                         NULL, &sync_event);
        if (!EFI_ERROR(st)) {
            st = gBS->SetTimer(sync_event, TimerPeriodic, (UINT64)sync_secs * 10000000);
        }
        if (EFI_ERROR(st)) {
            log_msg(LOG_WARN, "log: -logsync timer failed (%s)", efi_status_str(st));
        }
    }
    log_msg(LOG_INFO, "log: writing %s at exit%s", path,
            sync_secs ? " and every -logsync seconds" : "");
}

static const char *const level_tag[] = {"D ", "", "W ", "E "};

static void conout_write(const char *s, size_t n) {
    CHAR16 buf[130];
    size_t k = 0;
    if (!gST || !gST->ConOut) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            buf[k++] = L'\r';
        }
        /* Non-ASCII bytes (UTF-8 sequences) are shown as '?' on the console. */
        buf[k++] = c < 0x80 ? (CHAR16)c : L'?';
        if (k >= 128) {
            buf[k] = 0;
            gST->ConOut->OutputString(gST->ConOut, buf);
            k = 0;
        }
    }
    if (k) {
        buf[k] = 0;
        gST->ConOut->OutputString(gST->ConOut, buf);
    }
}

static void sinks_write(const char *s, size_t n) {
    ring_append(&ring, s, n);
#if DEBUGCON
    for (size_t i = 0; i < n; i++) {
        outb(0x403, (uint8_t)s[i]); /* our own QEMU isa-debugcon; never in hardware builds */
    }
#endif
    if (conout_on) {
        conout_write(s, n);
    }
}

void log_init(void) {
    ring_init(&ring, ring_buf, sizeof ring_buf, LOG_HEAD_SIZE);
    conout_on = true;
    at_line_start = true;
}

void log_set_conout(bool enabled) {
    conout_on = enabled;
}

void log_write(log_level level, const char *s, size_t n) {
    if ((unsigned)level > LOG_ERROR) {
        level = LOG_ERROR;
    }
    if (file_busy) {
        return; /* produced while writing the log file (e.g. fs errors): drop */
    }
    for (size_t start = 0; start < n;) {
        if (at_line_start && level_tag[level][0]) {
            sinks_write(level_tag[level], 2);
        }
        const char *nl = memchr(s + start, '\n', n - start);
        size_t end = nl ? (size_t)(nl - s) + 1 : n;
        sinks_write(s + start, end - start);
        at_line_start = nl != NULL;
        start = end;
    }
}

void log_vmsg(log_level level, const char *fmt, va_list ap) {
    char line[512];
    int r = vsnprintf(line, sizeof line - 1, fmt, ap);
    size_t n = r < 0 ? 0 : strnlen(line, sizeof line - 1);
    if (n == 0 || line[n - 1] != '\n') {
        line[n++] = '\n';
    }
    if (!at_line_start) {
        log_write(level, "\n", 1);
    }
    log_write(level, line, n);
}

void log_msg(log_level level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vmsg(level, fmt, ap);
    va_end(ap);
}

void log_finish(uint16_t exit_reason, uint64_t exit_status, uint64_t run_time_us) {
#if PROFILE_DEVLOOP
    /* Nothing may be appended to the ring while dlog_write reads it, so errors
     * from here on go to ConOut only. */
    dlog_write(&ring, exit_reason, exit_status, run_time_us, BUILD_ID, conout_write);
#else
    (void)exit_reason;
    (void)exit_status;
    (void)run_time_us;
#endif
}
