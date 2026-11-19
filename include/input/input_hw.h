/* Low-level keyboard/mouse access: discovery, takeover from firmware drivers,
 * raw data capture and restore. Used by HWPROBE directly and by the input
 * layer (decoders, keymaps, arbitration).
 * See docs/architecture.md#input-input. */
#ifndef UD_INPUT_HW_H
#define UD_INPUT_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- i8042 (PS/2 controller, ports 0x60/0x64) ----------------------------- */

/** True if the controller answers (status port does not read 0xFF). */
bool i8042_present(void);

/** Number of firmware console-input handles whose device path is a PS/2 keyboard. */
int i8042_firmware_handles(void);

/**
 * Disconnect the firmware PS/2 keyboard driver so we own port 0x60, and
 * register a cleanup that reconnects it. False if no firmware handle was found
 * (then polling would race the firmware and must not be used).
 */
bool i8042_take_over(void);

/** Give the keyboard back to firmware now (also runs at exit). Returns handles reconnected. */
int i8042_restore(void);

/**
 * With the keyboard taken over, also take the aux port (from any firmware PS/2
 * mouse driver) and start the aux device streaming. Returns its device ID
 * (0 = mouse, 3 = wheel mouse, 4 = 5-button) or -1 if none answers. Undone by
 * i8042_restore (and so at exit).
 */
int i8042_aux_start(void);

/** Read the controller command byte (needs takeover). Returns false on timeout. */
bool i8042_read_command_byte(uint8_t *out);

/** Drain pending bytes: data in buf, true in aux[] for touchpad/mouse bytes. */
int i8042_poll(uint8_t *buf, bool *aux, int max);

/* --- USB HID boot keyboards and mice -------------------------------------- */

#define USBHID_MAX_DEVS 8

typedef struct usbhid_dev usbhid_dev;

typedef struct {
    uint16_t vid, pid;
    uint8_t protocol; /* 1 = keyboard, 2 = mouse */
    uint8_t iface, endpoint, interval_ms;
    uint16_t max_packet;
} usbhid_desc;

/** Find HID boot-protocol keyboards and mice. Returns the count. */
int usbhid_enumerate(usbhid_dev **devs, int max);

const usbhid_desc *usbhid_describe(const usbhid_dev *d);

/**
 * Take the device from its firmware driver (exclusive open), select the boot
 * protocol, and start an async interrupt-IN transfer that queues reports.
 * Registers a cleanup that cancels it and reconnects the firmware driver.
 */
bool usbhid_start(usbhid_dev *d);

/** Stop and return the device to its firmware driver now (also runs at exit). */
void usbhid_stop(usbhid_dev *d);

/** Pop one queued report (up to 8 bytes) with its arrival time in us. */
bool usbhid_next_report(usbhid_dev *d, uint8_t report[8], size_t *len, uint64_t *t_us);

/** True once firmware has taken the device away (unplugged); it is then released. */
bool usbhid_lost(const usbhid_dev *d);

/** Reports dropped because the queue was full, and transfer errors seen. */
void usbhid_stats(const usbhid_dev *d, uint32_t *dropped, uint32_t *errors);

/* --- Firmware pointers (SIMPLE_POINTER / ABSOLUTE_POINTER) ---------------- */

#define FWPTR_MAX 8

typedef struct {
    bool absolute;
    bool console;          /* the console's merged pointer (all firmware-driven devices) */
    char path[96];         /* device path summary, e.g. "/ACPI(PNP0a03)/1.1/USB(2)" */
    uint64_t res_x, res_y; /* SIMPLE: counts per mm (0 = unknown) */
    uint8_t buttons;       /* SIMPLE: bit 0 left, bit 1 right present */
    uint64_t min_x, max_x, min_y, max_y; /* ABSOLUTE: coordinate ranges */
    uint32_t attributes;                 /* ABSOLUTE: EFI_ABSP_* */
} fwptr_desc;

/** Find every firmware pointer (resetting each); returns the count. */
int fwptr_enumerate(void);

const fwptr_desc *fwptr_describe(int i);

/**
 * Read pointer i: 1 with buttons and normalised motion (dy positive = away
 * from the user), 0 if nothing new, -1 on a device error.
 */
int fwptr_read(int i, int *buttons, int *dx, int *dy);

/* --- UEFI console input ---------------------------------------------------- */

typedef struct {
    bool has_ex;     /* EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL on ConIn */
    bool exposed_ok; /* SetState(EFI_KEY_STATE_EXPOSED) accepted */
    uint64_t exposed_status;
} conin_info;

/** Probe ConIn capabilities; tries to enable modifier-only keystrokes. */
void conin_probe(conin_info *out);

/** Non-blocking read; true if a keystroke was returned (scan code, char, shift state). */
bool conin_read(uint16_t *scan, uint16_t *ch, uint32_t *shift_state);

/** Block until a keystroke is available on ConIn (not consumed: read it with
 *  conin_read); false after timeout_ms (0 = forever). */
bool conin_wait_key(uint32_t timeout_ms);

#endif
