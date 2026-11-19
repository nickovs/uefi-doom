/* i8042 discovery, takeover from the firmware PS/2 keyboard (and mouse) drivers,
 * raw polling, and streaming from the aux (mouse/touchpad) device. */
#include <stdio.h>
#include <stdlib.h>

#include "efi/efi.h"

#include <Protocol/DevicePath.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/SimpleTextIn.h>

#include "efi/clock.h"
#include "efi/cpu.h"
#include "efi/efi_main.h"
#include "efi/log.h"
#include "input/input_hw.h"

EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiSimpleTextInProtocolGuid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
extern EFI_GUID gEfiSimplePointerProtocolGuid; /* fwptr.c */

#define I8042_DATA 0x60
#define I8042_STATUS 0x64
#define STATUS_OBF 0x01 /* output buffer full: a byte to read */
#define STATUS_IBF 0x02 /* input buffer full: controller busy */
#define STATUS_AUX 0x20 /* byte came from the aux (mouse) port */
#define MAX_PS2_HANDLES 4

static EFI_HANDLE taken[MAX_PS2_HANDLES];
static int ntaken;
static EFI_HANDLE mice[MAX_PS2_HANDLES]; /* firmware PS/2 mouse drivers we disconnected */
static int nmice;
static bool aux_on;
static uint8_t saved_cb;

bool i8042_present(void) {
    return inb(I8042_STATUS) != 0xff;
}

/* Does the device path contain an ACPI node with one of these EISA PNP IDs? */
static bool path_has_pnp(EFI_DEVICE_PATH_PROTOCOL *dp, UINT32 a, UINT32 b) {
    for (int guard = 0; dp && guard < 64; guard++) {
        size_t len = (size_t)dp->Length[0] | (size_t)dp->Length[1] << 8;
        if (dp->Type == END_DEVICE_PATH_TYPE || len < 4) {
            return false;
        }
        if (dp->Type == ACPI_DEVICE_PATH && dp->SubType == ACPI_DP &&
            len >= sizeof(ACPI_HID_DEVICE_PATH)) {
            UINT32 hid = ((ACPI_HID_DEVICE_PATH *)dp)->HID;
            if (hid == a || hid == b) {
                return true;
            }
        }
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((uint8_t *)dp + len);
    }
    return false;
}

static bool is_ps2_keyboard(EFI_DEVICE_PATH_PROTOCOL *dp) {
    for (int guard = 0; dp && guard < 64; guard++) {
        size_t len = (size_t)dp->Length[0] | (size_t)dp->Length[1] << 8;
        if (dp->Type == END_DEVICE_PATH_TYPE || len < 4) {
            return false;
        }
        if (dp->Type == ACPI_DEVICE_PATH && dp->SubType == ACPI_DP &&
            len >= sizeof(ACPI_HID_DEVICE_PATH)) {
            UINT32 hid = ((ACPI_HID_DEVICE_PATH *)dp)->HID;
            if (hid == EISA_PNP_ID(0x0303) || hid == EISA_PNP_ID(0x030B)) {
                return true;
            }
        }
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((uint8_t *)dp + len);
    }
    return false;
}

/* Firmware PS/2 keyboard handles, up to max. */
static int find_handles(EFI_HANDLE *out, int max) {
    UINTN n = 0;
    EFI_HANDLE *hs = NULL;
    if (EFI_ERROR(
            gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleTextInProtocolGuid, NULL, &n, &hs))) {
        return 0;
    }
    int found = 0;
    for (UINTN i = 0; i < n && found < max; i++) {
        EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
        if (!EFI_ERROR(gBS->HandleProtocol(hs[i], &gEfiDevicePathProtocolGuid, (void **)&dp)) &&
            is_ps2_keyboard(dp)) {
            out[found++] = hs[i];
        }
    }
    gBS->FreePool(hs);
    return found;
}

int i8042_firmware_handles(void) {
    EFI_HANDLE hs[MAX_PS2_HANDLES];
    return find_handles(hs, MAX_PS2_HANDLES);
}

static void aux_stop(void);

int i8042_restore(void) {
    aux_stop(); /* before the keyboard driver comes back */
    int ok = 0;
    for (int i = 0; i < ntaken; i++) {
        EFI_STATUS st = gBS->ConnectController(taken[i], NULL, NULL, TRUE);
        log_msg(EFI_ERROR(st) ? LOG_WARN : LOG_INFO, "i8042: reconnect firmware driver: %s",
                efi_status_str(st));
        ok += !EFI_ERROR(st);
    }
    ntaken = 0;
    return ok;
}

static void restore_cleanup(void *ctx) {
    (void)ctx;
    i8042_restore();
}

bool i8042_take_over(void) {
    EFI_HANDLE hs[MAX_PS2_HANDLES];
    int n = find_handles(hs, MAX_PS2_HANDLES);
    if (n == 0) {
        log_msg(LOG_WARN, "i8042: no firmware PS/2 keyboard handle; not taking over");
        return false;
    }
    for (int i = 0; i < n; i++) {
        EFI_STATUS st = gBS->DisconnectController(hs[i], NULL, NULL);
        log_msg(EFI_ERROR(st) ? LOG_WARN : LOG_INFO, "i8042: disconnect firmware driver: %s",
                efi_status_str(st));
        if (!EFI_ERROR(st)) {
            if (ntaken == 0) {
                efi_cleanup_push(restore_cleanup, NULL, "i8042");
            }
            taken[ntaken++] = hs[i];
        }
    }
    return ntaken > 0;
}

static bool wait_status(uint8_t mask, bool set, uint32_t timeout_us) {
    uint64_t end = clock_us() + timeout_us;
    while (((inb(I8042_STATUS) & mask) != 0) != set) {
        if (clock_us() > end) {
            return false;
        }
    }
    return true;
}

bool i8042_read_command_byte(uint8_t *out) {
    if (!ntaken || !wait_status(STATUS_IBF, false, 10000)) {
        return false;
    }
    outb(I8042_STATUS, 0x20);
    if (!wait_status(STATUS_OBF, true, 20000)) {
        return false;
    }
    *out = inb(I8042_DATA);
    return true;
}

int i8042_poll(uint8_t *buf, bool *aux, int max) {
    int n = 0;
    uint8_t st;
    /* Bounded: a stuck OBF bit must not hang the caller. */
    while (n < max && n < 64 && ((st = inb(I8042_STATUS)) & STATUS_OBF)) {
        buf[n] = inb(I8042_DATA);
        aux[n] = (st & STATUS_AUX) != 0;
        n++;
    }
    return n;
}

/* --- Aux (mouse / touchpad) port --------------------------------------------- */

static bool write_port(uint16_t port, uint8_t v) {
    if (!wait_status(STATUS_IBF, false, 10000)) {
        return false;
    }
    outb(port, v);
    return true;
}

#define AUX_REPLY_US 300000 /* slow ECs: Linux allows hundreds of ms too */

/* Start-up trace, logged once: each command and every byte seen after it,
 * "k" marking bytes without the controller's aux flag (dropped). */
static char aux_trace[192];
static size_t aux_trace_len;

static void trace(const char *fmt, unsigned v) {
    if (aux_trace_len < sizeof aux_trace - 8) {
        aux_trace_len +=
            (size_t)snprintf(aux_trace + aux_trace_len, sizeof aux_trace - aux_trace_len, fmt, v);
    }
}

/* Next aux byte within the timeout (keyboard bytes meanwhile are dropped), or -1. */
static int read_aux(uint32_t timeout_us) {
    uint64_t end = clock_us() + timeout_us;
    while (clock_us() < end) {
        uint8_t st = inb(I8042_STATUS);
        if (st & STATUS_OBF) {
            uint8_t b = inb(I8042_DATA);
            if (st & STATUS_AUX) {
                trace(" %02x", b);
                return b;
            }
            trace(" %02xk", b);
        }
    }
    trace(" timeout%.0u", 0);
    return -1;
}

/* Send a byte to the aux device; true if it acknowledged (0xFA). One retry on
 * 0xFE (resend). */
static bool aux_cmd(uint8_t b) {
    for (int attempt = 0; attempt < 2; attempt++) {
        trace(" |%02x:", b);
        if (!write_port(I8042_STATUS, 0xd4) || !write_port(I8042_DATA, b)) {
            trace(" busy%.0u", 0);
            return false;
        }
        int r = read_aux(AUX_REPLY_US);
        if (r != 0xfe) {
            return r == 0xfa;
        }
    }
    return false;
}

static void aux_stop(void) {
    if (aux_on) {
        aux_cmd(0xf5); /* stop reporting */
        uint8_t junk[64];
        bool a[64];
        i8042_poll(junk, a, 64);
        write_port(I8042_STATUS, 0x60);
        write_port(I8042_DATA, saved_cb);
        if (saved_cb & 0x20) {
            write_port(I8042_STATUS, 0xa7); /* it was disabled: disable it again */
        }
        aux_on = false;
    }
    for (int i = 0; i < nmice; i++) {
        EFI_STATUS st = gBS->ConnectController(mice[i], NULL, NULL, TRUE);
        log_msg(EFI_ERROR(st) ? LOG_WARN : LOG_INFO, "i8042: reconnect firmware mouse driver: %s",
                efi_status_str(st));
    }
    nmice = 0;
}

int i8042_aux_start(void) {
    if (!ntaken || aux_on) {
        return -1;
    }
    /* Take the port from a firmware PS/2 mouse driver, if there is one. */
    UINTN n = 0;
    EFI_HANDLE *hs = NULL;
    if (!EFI_ERROR(
            gBS->LocateHandleBuffer(ByProtocol, &gEfiSimplePointerProtocolGuid, NULL, &n, &hs))) {
        for (UINTN i = 0; i < n && nmice < MAX_PS2_HANDLES; i++) {
            EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
            if (!EFI_ERROR(gBS->HandleProtocol(hs[i], &gEfiDevicePathProtocolGuid, (void **)&dp)) &&
                path_has_pnp(dp, EISA_PNP_ID(0x0F03), EISA_PNP_ID(0x0F13)) &&
                !EFI_ERROR(gBS->DisconnectController(hs[i], NULL, NULL))) {
                mice[nmice++] = hs[i];
            }
        }
        gBS->FreePool(hs);
    }
    if (!i8042_read_command_byte(&saved_cb)) {
        aux_stop();
        return -1;
    }
    aux_on = true;
    /* Enable the port and its clock; no aux interrupt (we poll, and an
     * unexpected IRQ 12 could upset the firmware). */
    int id = -1;
    if (write_port(I8042_STATUS, 0xa8) && write_port(I8042_STATUS, 0x60) &&
        write_port(I8042_DATA, (uint8_t)(saved_cb & ~0x22)) && aux_cmd(0xf6) && aux_cmd(0xf2)) {
        id = read_aux(50000);
    }
    if (id < 0 || !aux_cmd(0xf4)) {
        log_msg(LOG_INFO, "i8042: no aux device answers (cb 0x%02x;%s)", saved_cb, aux_trace);
        aux_stop();
        return -1;
    }
    log_msg(LOG_INFO,
            "i8042: aux device id 0x%02x streaming (%d firmware mouse driver(s) "
            "disconnected; cb 0x%02x;%s)",
            id, nmice, saved_cb, aux_trace);
    return id;
}
