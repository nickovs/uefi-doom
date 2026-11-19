/* HWPROBE.EFI: real-hardware probe. See docs/architecture.md#hwprobe.
 * Output: HWPROBE.LOG (the log), HWPROBE.JSON, ACPI\*.BIN in the image dir. */
#include <stdio.h>
#include <string.h>

#include "efi/efi.h"

#include <Guid/GlobalVariable.h>

#include "efi/acpi.h"
#include "efi/argv_core.h"
#include "efi/cpu.h"
#include "efi/efi_main.h"
#include "input/input_hw.h"
#include "efi/fs.h"
#include "efi/log.h"
#include "hwprobe.h"

EFI_GUID gEfiGlobalVariableGuid = EFI_GLOBAL_VARIABLE;

const char app_name[] = "hwprobe";
bool hw_batch;
bool hw_wc;
bool hw_avx;
bool hw_patwc;

void probe_system(void) {
    char vendor[64] = "?";
    if (gST->FirmwareVendor) {
        argv_ucs2_to_utf8((const uint16_t *)gST->FirmwareVendor, 63, vendor, sizeof vendor);
    }
    uint32_t r[4];
    char cpu_vendor[13] = {0};
    cpuid(0, 0, r);
    memcpy(cpu_vendor, &r[1], 4);
    memcpy(cpu_vendor + 4, &r[3], 4);
    memcpy(cpu_vendor + 8, &r[2], 4);
    char brand[49] = "?";
    cpuid(0x80000000, 0, r);
    if (r[0] >= 0x80000004) {
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, 0, r);
            memcpy(brand + 16 * i, r, 16);
        }
        brand[48] = 0;
    }
    const char *b = brand;
    while (*b == ' ') {
        b++;
    }

    UINT8 sb = 0;
    UINTN sz = sizeof sb;
    EFI_STATUS st = gRT->GetVariable(L"SecureBoot", &gEfiGlobalVariableGuid, NULL, &sz, &sb);

    log_msg(LOG_INFO, "== system");
    log_msg(LOG_INFO, "firmware %s rev 0x%08x, UEFI %u.%u; CPU %s %s; Secure Boot %s", vendor,
            gST->FirmwareRevision, gST->Hdr.Revision >> 16, (gST->Hdr.Revision & 0xffff) / 10,
            cpu_vendor, b,
            EFI_ERROR(st) ? "unknown"
            : sb          ? "on"
                          : "off");
    rep_obj("system");
    rep_str("firmware_vendor", vendor);
    rep_hex("firmware_revision", gST->FirmwareRevision);
    rep_hex("uefi_revision", gST->Hdr.Revision);
    rep_str("cpu_vendor", cpu_vendor);
    rep_str("cpu_brand", b);
    if (!EFI_ERROR(st)) {
        rep_bool("secure_boot", sb != 0);
    }
    rep_str("build", BUILD_ID);
    rep_end();
}

static void dump_table(const char *sig, const uint8_t *t, size_t len, void *ctx) {
    unsigned *count = ctx;
    char path[32];
    snprintf(path, sizeof path, "ACPI/%s%u.BIN", sig, count[0]++);
    int r = fs_write_whole(path, t, len);
    rep_obj(NULL);
    rep_str("sig", sig);
    rep_int("len", (int64_t)len);
    rep_str("file", r == 0 ? path : "");
    rep_end();
}

void probe_acpi(void) {
    log_msg(LOG_INFO, "== acpi");
    rep_obj("acpi");
    acpi_fadt_info f;
    if (acpi_fadt(&f)) {
        log_msg(LOG_INFO,
                "FADT rev %u: IAPC_BOOT_ARCH 0x%04x (8042=%d legacy=%d) flags 0x%08x "
                "hw_reduced=%d PM timer port 0x%x %s",
                f.revision, f.iapc_boot_arch, !!(f.iapc_boot_arch & ACPI_IAPC_8042),
                !!(f.iapc_boot_arch & ACPI_IAPC_LEGACY_DEVICES), f.flags, f.hw_reduced,
                f.pm_tmr_port, f.pm_tmr_32bit ? "32-bit" : "24-bit");
        rep_int("fadt_revision", f.revision);
        rep_hex("iapc_boot_arch", f.iapc_boot_arch);
        rep_bool("fadt_8042", f.iapc_boot_arch & ACPI_IAPC_8042);
        rep_hex("fadt_flags", f.flags);
        rep_hex("pm_timer_port", f.pm_tmr_port);
        rep_bool("pm_timer_32bit", f.pm_tmr_32bit);
    }
    static const char *const ids[] = {"PNP0303", "PNP030B", "PNP0F13",
                                      "PNP0800", "PNP0100", "PNP0C50"};
    rep_obj("eisaid_hints");
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        int n = acpi_eisaid_count(ids[i]);
        log_msg(LOG_INFO, "DSDT/SSDT %s: %d", ids[i], n);
        rep_int(ids[i], n);
    }
    rep_end();
    fs_mkdir("ACPI");
    unsigned count = 0;
    rep_arr("tables");
    acpi_for_each(dump_table, &count);
    rep_end();
    log_msg(LOG_INFO, "dumped %u ACPI tables to ACPI/", count);
    rep_end();
}

int app_main(int argc, char **argv) {
    bool hang = false;
    const char *only = NULL;
    hw_batch = true; /* unattended unless asked: safe for boot-menu and systest runs */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-interactive") == 0) {
            hw_batch = false;
        } else if (strcmp(argv[i], "-patwc") == 0) {
            hw_patwc = true;
        } else if (strcmp(argv[i], "-avx") == 0) {
            hw_avx = true;
        } else if (strcmp(argv[i], "-wc") == 0) {
            hw_wc = true;
        } else if (strcmp(argv[i], "-batch") == 0) {
            hw_batch = true;
        } else if (strcmp(argv[i], "-hang") == 0) {
            hang = true;
        } else if (strcmp(argv[i], "-only") == 0 && i + 1 < argc) {
            only = argv[++i]; /* run a single section, e.g. -only video */
        }
    }
    if (hang) {
        log_msg(LOG_WARN, "hwprobe: -hang given; spinning until the watchdog fires");
        for (;;) {
        }
    }
    log_msg(LOG_INFO, "hwprobe: %s mode", hw_batch ? "batch" : "interactive");
    if (!hw_batch) {
        /* Waiting for a person must not trip the boot manager's 5-minute watchdog. */
        gBS->SetWatchdogTimer(1800, 0x10000, 0, NULL);
        log_msg(LOG_INFO, "hwprobe: watchdog extended to 30 minutes for the interactive run");
        log_msg(LOG_INFO, "");
        log_msg(LOG_INFO, ">>> HWPROBE interactive run. Follow the >>> prompts on this screen.");
        log_msg(LOG_INFO, ">>> Press any key to start (waits up to 10 minutes).");
        if (conin_wait_key(600000)) {
            uint16_t s, c;
            uint32_t sh;
            conin_read(&s, &c, &sh);
        }
    }

    static const struct {
        const char *name;
        void (*fn)(void);
    } sections[] = {
        {"system", probe_system}, {"acpi", probe_acpi},   {"clock", probe_clock},
        {"video", probe_video},   {"input", probe_input}, {"speaker", probe_speaker},
    };
    rep_obj(NULL);
    for (unsigned i = 0; i < sizeof sections / sizeof sections[0]; i++) {
        if (!only || strcmp(only, sections[i].name) == 0) {
            sections[i].fn();
        }
    }
    int r = rep_save("HWPROBE.JSON");
    log_msg(r == 0 ? LOG_INFO : LOG_WARN, "hwprobe: HWPROBE.JSON %s",
            r == 0 ? "written" : "could not be written");
    log_msg(LOG_INFO, "hwprobe: done");
    return 0;
}
