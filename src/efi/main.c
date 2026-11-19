/* UEFI entry point, single exit path and cleanup registry.
 * See docs/architecture.md#lifecycle. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efi/efi.h"

#include <Protocol/LoadedImage.h>
#include <Protocol/ShellParameters.h>

#include "efi/alloc.h"
#include "efi/argv_core.h"
#include "efi/clock.h"
#include "efi/efi_main.h"
#include "efi/fs.h"
#include "efi/log.h"

#define MAX_CLEANUPS 64
#define MAX_ARGS 64
#define ARGBUF_SIZE 4096

EFI_HANDLE gImage;
EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;

EFI_GUID gEfiLoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiShellParametersProtocolGuid = EFI_SHELL_PARAMETERS_PROTOCOL_GUID;

static struct {
    cleanup_fn fn;
    void *ctx;
    const char *name;
} cleanups[MAX_CLEANUPS];
static int ncleanups;

static void *exit_jmp[5];
static exit_reason exit_why;
static bool exiting;

static char argbuf[ARGBUF_SIZE];
static char *argv_store[MAX_ARGS];

void efi_cleanup_push(cleanup_fn fn, void *ctx, const char *name) {
    if (ncleanups >= MAX_CLEANUPS) {
        log_msg(LOG_ERROR, "cleanup registry full registering '%s'", name);
        efi_exit(EXIT_REASON_FATAL);
    }
    cleanups[ncleanups].fn = fn;
    cleanups[ncleanups].ctx = ctx;
    cleanups[ncleanups].name = name;
    ncleanups++;
}

static void run_cleanups(void) {
    while (ncleanups > 0) {
        ncleanups--;
        log_msg(LOG_DEBUG, "cleanup: %s", cleanups[ncleanups].name);
        cleanups[ncleanups].fn(cleanups[ncleanups].ctx);
    }
}

_Noreturn void efi_exit(exit_reason reason) {
    if (exiting) {
        /* A cleanup called exit(): keep the first reason, skip the rest of that cleanup. */
        __builtin_longjmp(exit_jmp, 1);
    }
    exiting = true;
    exit_why = reason;
    __builtin_longjmp(exit_jmp, 1);
}

/* argv from the Shell's parameters protocol, else from boot-entry LoadOptions. */
static int build_argv(void) {
    EFI_SHELL_PARAMETERS_PROTOCOL *sp = NULL;
    if (!EFI_ERROR(gBS->HandleProtocol(gImage, &gEfiShellParametersProtocolGuid, (void **)&sp)) &&
        sp && sp->Argc > 0) {
        size_t pos = 0;
        int argc = 0;
        for (UINTN i = 0; i < sp->Argc && argc < MAX_ARGS - 1; i++) {
            int n = argv_ucs2_to_utf8((const uint16_t *)sp->Argv[i], SIZE_MAX, argbuf + pos,
                                      sizeof argbuf - pos);
            if (n < 0) {
                log_msg(LOG_WARN, "argv: Shell arguments truncated at %d", argc);
                break;
            }
            argv_store[argc++] = argbuf + pos;
            pos += (size_t)n + 1;
        }
        argv_store[argc] = NULL;
        log_msg(LOG_INFO, "argv: %d args from Shell", argc);
        return argc;
    }

    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    const uint16_t *opts = NULL;
    size_t size = 0;
    if (!EFI_ERROR(gBS->HandleProtocol(gImage, &gEfiLoadedImageProtocolGuid, (void **)&li)) && li) {
        opts = li->LoadOptions;
        size = li->LoadOptionsSize;
    }
    int argc =
        argv_from_load_options(opts, size, app_name, argbuf, sizeof argbuf, argv_store, MAX_ARGS);
    if (argc < 0) {
        log_msg(LOG_WARN, "argv: LoadOptions (%zu bytes) too long; ignored", size);
        argc =
            argv_from_load_options(NULL, 0, app_name, argbuf, sizeof argbuf, argv_store, MAX_ARGS);
    } else if (size && !argv_is_text(opts, size)) {
        log_msg(LOG_INFO, "argv: LoadOptions (%zu bytes) are binary data; ignored", size);
    }
    log_msg(LOG_INFO, "argv: %d args from LoadOptions", argc);
    return argc;
}

/* Value of integer option `name` (e.g. "-watchdog 600"), or `dflt` if absent. */
static int int_arg(int argc, char **argv, const char *name, int dflt) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return atoi(argv[i + 1]);
        }
    }
    return dflt;
}

static void setup_watchdog(int argc, char **argv) {
#if PROFILE_RELEASE
    (void)argc;
    (void)argv;
    gBS->SetWatchdogTimer(0, 0, 0, NULL);
    log_msg(LOG_INFO, "watchdog: disabled");
#else
    int secs = int_arg(argc, argv, "-watchdog", -1);
    if (secs >= 0) {
        EFI_STATUS st = gBS->SetWatchdogTimer((UINTN)secs, 0x10000, 0, NULL);
        log_msg(LOG_INFO, "watchdog: re-armed for %d s (%s)", secs, efi_status_str(st));
        return;
    }
    log_msg(LOG_INFO, "watchdog: left as set by firmware (devloop)");
#endif
}

static EFI_STATUS status_for(exit_reason r) {
#if PROFILE_DEVLOOP
    (void)r;
    return 1; /* non-zero warning: boot manager moves on to the next BootOrder entry */
#else
    return r == EXIT_REASON_QUIT ? EFI_SUCCESS : EFI_LOAD_ERROR;
#endif
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    gImage = image;
    gST = st;
    gBS = st->BootServices;
    gRT = st->RuntimeServices;

    if (st->ConOut) {
        st->ConOut->ClearScreen(st->ConOut); /* firmware may leave its boot logo up */
    }
    log_init();
    clock_init();
    log_msg(LOG_INFO, "%s %s starting", app_name, BUILD_ID);
    char vendor[64] = "?";
    if (st->FirmwareVendor) {
        argv_ucs2_to_utf8((const uint16_t *)st->FirmwareVendor, 63, vendor, sizeof vendor);
    }
    log_msg(LOG_INFO, "firmware: %s rev 0x%08x, UEFI %u.%u", vendor, st->FirmwareRevision,
            st->Hdr.Revision >> 16, (st->Hdr.Revision & 0xffff) / 10);

    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    if (!EFI_ERROR(gBS->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (void **)&li)) && li) {
        log_msg(LOG_INFO, "image base %p size 0x%llx", li->ImageBase,
                (unsigned long long)li->ImageSize);
    }

    int argc = build_argv();
    for (int i = 0; i < argc; i++) {
        log_msg(LOG_INFO, "argv[%d] = \"%s\"", i, argv_store[i]);
    }
    setup_watchdog(argc, argv_store);
    if (fs_init()) {
        char path[24];
        snprintf(path, sizeof path, "%s.LOG", app_name);
        for (char *c = path; *c; c++) {
            *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
        }
        log_file_enable(path, (uint32_t)int_arg(argc, argv_store, "-logsync", 0));
    }

    if (__builtin_setjmp(exit_jmp) == 0) {
        int rc = app_main(argc, argv_store);
        exiting = true;
        exit_why = rc == 0 ? EXIT_REASON_QUIT : EXIT_REASON_FATAL;
    }

    run_cleanups();
    EFI_STATUS status = status_for(exit_why);
    log_msg(LOG_INFO, "exit: reason %d, status %s, %zu live heap blocks before free", exit_why,
            efi_status_str(status), alloc_live_count());
    log_finish((uint16_t)exit_why, status, clock_us());
    alloc_free_all();
    return status;
}
