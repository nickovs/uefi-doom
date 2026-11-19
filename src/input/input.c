/* Input arbitration. See include/input/input.h. */
#include "input/input.h"

#include <string.h>

#include "efi/clock.h"
#include "efi/efi_main.h"
#include "efi/idle.h"
#include "efi/log.h"
#include "input/conin_core.h"
#include "input/hid_core.h"
#include "input/i8042_core.h"
#include "input/input_hw.h"
#include "input/pointer_core.h"

#define QUEUE 128 /* power of two */
#define CONIN_INITIAL_HOLD_US 100000u
#define USB_ERROR_LIMIT 8 /* transfer errors before a device counts as gone */
#define CB_TRANSLATE 0x40 /* i8042 command byte: set 2 -> set 1 translation */

enum { SRC_I8042, SRC_CONIN, SRC_USB0, NSRC = SRC_USB0 + USBHID_MAX_DEVS };

static input_info info;
static bool ready;
static i8042_dec dec;
static conin_pulse pulse;
static struct usb {
    usbhid_dev *dev;
    bool kbd, live;
    hid_kbd state;
} usb[USBHID_MAX_DEVS];
static int nusb;

static uint8_t src_held[NSRC][32]; /* per-source key state (dedupes typematic repeats) */
static uint8_t refs[256];          /* sources holding each key */
static struct {
    key_event ev[QUEUE];
    unsigned head, tail;
} q;
/* Mouse motion accumulates across sources; buttons are kept per source and
 * OR-ed, so one device cannot release another's button. */
enum { MSRC_AUX, MSRC_FW0, MSRC_USB0 = MSRC_FW0 + FWPTR_MAX, NMSRC = MSRC_USB0 + USBHID_MAX_DEVS };
static uint8_t msrc_buttons[NMSRC];
static int mouse_buttons, mouse_dx, mouse_dy;
static bool mouse_changed;
static ps2mouse_dec aux_dec;
static int fw_ids[FWPTR_MAX], nfw;

static uint32_t mouse_reports[3]; /* aux, firmware, USB: for the exit log */

static void mouse_add(int src, int buttons, int dx, int dy) {
    mouse_reports[src == MSRC_AUX ? 0 : src < MSRC_USB0 ? 1 : 2]++;
    msrc_buttons[src] = (uint8_t)buttons;
    int all = 0;
    for (int i = 0; i < NMSRC; i++) {
        all |= msrc_buttons[i];
    }
    mouse_changed |= all != mouse_buttons || dx || dy;
    mouse_buttons = all;
    mouse_dx += dx;
    mouse_dy += dy;
}

static void emit(key_event ev) {
    if (q.head - q.tail < QUEUE) {
        q.ev[q.head++ % QUEUE] = ev;
    }
}

/* One source's key transition -> merged key state across sources. */
static void push(int src, key_event ev) {
    uint8_t *h = &src_held[src][ev.key >> 3], bit = (uint8_t)(1u << (ev.key & 7));
    if (ev.pressed == !!(*h & bit)) {
        return; /* repeat, or release of a key this source never pressed */
    }
    *h ^= bit;
    if (ev.pressed) {
        *(src == SRC_I8042   ? &info.presses_i8042
          : src == SRC_CONIN ? &info.presses_conin
                             : &info.presses_usb) += 1;
        if (refs[ev.key]++ == 0) {
            emit(ev);
        }
        if (ev.key == DK_BACKSPACE && refs[DK_FIRE] && refs[DK_RALT]) {
            log_msg(LOG_WARN, "input: Ctrl+Alt+Backspace: emergency exit");
            efi_exit(EXIT_REASON_EMERGENCY);
        }
    } else if (--refs[ev.key] == 0) {
        emit(ev);
    }
}

static void start_i8042(void) {
    if (!i8042_present() || !i8042_take_over()) {
        return;
    }
    uint8_t cb;
    if (!i8042_read_command_byte(&cb)) {
        log_msg(LOG_WARN, "input: i8042 command byte unreadable; not using it");
        i8042_restore();
        return;
    }
    if (!(cb & CB_TRANSLATE)) {
        /* shortcut: only translated set 1 is decoded; add set 2 if a laptop needs it */
        log_msg(LOG_WARN, "input: i8042 translation off (command byte 0x%02x); not using it", cb);
        i8042_restore();
        return;
    }
    uint8_t junk[64];
    bool aux[64];
    i8042_poll(junk, aux, 64); /* flush */
    info.i8042 = true;
}

static void start_usb(bool mice) {
    usbhid_dev *devs[USBHID_MAX_DEVS];
    int n = usbhid_enumerate(devs, USBHID_MAX_DEVS);
    for (int i = 0; i < n; i++) {
        const usbhid_desc *d = usbhid_describe(devs[i]);
        if ((d->protocol != 1 && !mice) || !usbhid_start(devs[i])) {
            continue;
        }
        usb[nusb] = (struct usb){.dev = devs[i], .kbd = d->protocol == 1, .live = true};
        nusb++;
        if (d->protocol == 1) {
            info.usb_kbds++;
        } else {
            info.usb_mice++;
        }
        log_msg(LOG_INFO, "input: USB %s %04x:%04x", d->protocol == 1 ? "keyboard" : "mouse",
                d->vid, d->pid);
    }
}

static void stats_cleanup(void *ctx) {
    (void)ctx;
    log_msg(LOG_INFO, "input: key presses i8042=%u usb=%u conin=%u", info.presses_i8042,
            info.presses_usb, info.presses_conin);
    log_msg(LOG_INFO, "input: mouse reports ps2=%u firmware=%u usb=%u", mouse_reports[0],
            mouse_reports[1], mouse_reports[2]);
}

/* Firmware pointers left after the takeovers: the console's merged pointers
 * if the firmware has them (one read covers every device it drives), else
 * each device. */
static void start_firmware_pointers(void) {
    int n = fwptr_enumerate(), consoles = 0;
    for (int i = 0; i < n; i++) {
        consoles += fwptr_describe(i)->console;
    }
    for (int i = 0; i < n && nfw < FWPTR_MAX; i++) {
        const fwptr_desc *d = fwptr_describe(i);
        if (!consoles || d->console) {
            fw_ids[nfw++] = i;
            log_msg(LOG_INFO, "input: firmware %s pointer %s", d->absolute ? "absolute" : "simple",
                    d->console ? "(console)" : d->path);
        }
    }
    info.fw_pointers = nfw;
}

void input_init(input_mode mode, mouse_mode mice) {
    efi_cleanup_push(stats_cleanup, NULL, "input stats"); /* runs after devices are returned */
    if (mode != INPUT_CONIN) {
        start_i8042();
        start_usb(mice == MOUSE_AUTO);
        if (info.i8042 && mice == MOUSE_AUTO) {
            info.ps2_mouse = i8042_aux_start() >= 0;
        }
    }
    if (mice != MOUSE_OFF) {
        start_firmware_pointers();
    }
    info.conin = !info.i8042 && !info.usb_kbds;
    if (info.conin) {
        conin_info ci;
        conin_probe(&ci);
        conin_pulse_init(&pulse, CONIN_INITIAL_HOLD_US);
        if (mode == INPUT_RAW) {
            log_msg(LOG_WARN, "input: -input raw, but no raw keyboard; using ConIn");
        }
        log_msg(LOG_INFO, "input: ConIn%s fallback, modifier keys %s", ci.has_ex ? " Ex" : "",
                ci.exposed_ok ? "reported" : "not reported");
    }
    log_msg(LOG_INFO,
            "input: i8042 %s, %d USB keyboard(s), %d USB mouse/mice, PS/2 mouse %s, %d firmware "
            "pointer(s)%s",
            info.i8042 ? "on" : "off", info.usb_kbds, info.usb_mice, info.ps2_mouse ? "on" : "off",
            info.fw_pointers, info.conin ? ", ConIn" : "");
    ready = true;
}

static void poll_i8042(void) {
    uint8_t buf[64];
    bool aux[64];
    int n = i8042_poll(buf, aux, 64);
    for (int i = 0; i < n; i++) {
        key_event ev[2];
        if (aux[i]) {
            int b, dx, dy;
            if (info.ps2_mouse && ps2mouse_byte(&aux_dec, buf[i], &b, &dx, &dy)) {
                mouse_add(MSRC_AUX, b, dx, dy); /* PS/2: positive dy is away from the user */
            }
            continue;
        }
        int k = i8042_dec_byte(&dec, buf[i], ev);
        for (int j = 0; j < k; j++) {
            push(SRC_I8042, ev[j]);
        }
    }
}

static void poll_usb(int i) {
    struct usb *u = &usb[i];
    uint8_t rep[8];
    size_t len;
    uint64_t t;
    key_event ev[HID_KBD_MAX_EVENTS];
    while (usbhid_next_report(u->dev, rep, &len, &t)) {
        if (u->kbd) {
            int n = hid_kbd_report(&u->state, rep, len, ev);
            for (int j = 0; j < n; j++) {
                push(SRC_USB0 + i, ev[j]);
            }
        } else {
            int b, dx, dy;
            if (hid_mouse_report(rep, len, &b, &dx, &dy)) {
                mouse_add(MSRC_USB0 + i, b, dx, -dy); /* HID: positive is towards the user */
            }
        }
    }
    uint32_t dropped, errors;
    usbhid_stats(u->dev, &dropped, &errors);
    bool lost = usbhid_lost(u->dev);
    if (lost || errors >= USB_ERROR_LIMIT) {
        log_msg(LOG_WARN, "input: USB %04x:%04x %s; releasing its keys",
                usbhid_describe(u->dev)->vid, usbhid_describe(u->dev)->pid,
                lost ? "unplugged" : "failing");
        u->live = false;
        int n = hid_kbd_release_all(&u->state, ev);
        for (int j = 0; j < n; j++) {
            push(SRC_USB0 + i, ev[j]);
        }
    }
}

static void poll_conin(void) {
    uint16_t scan, ch;
    uint32_t shift;
    uint64_t now = clock_us();
    while (conin_read(&scan, &ch, &shift)) {
        uint8_t keys[4];
        int n = conin_map_modifiers(shift, keys);
        keys[n++] = conin_map_key(scan, ch);
        for (int i = 0; i < n; i++) {
            if (keys[i] && conin_pulse_stroke(&pulse, keys[i], now)) {
                push(SRC_CONIN, (key_event){1, keys[i]});
            }
        }
    }
    for (int k; (k = conin_pulse_expire(&pulse, now)) >= 0;) {
        push(SRC_CONIN, (key_event){0, (unsigned char)k});
    }
}

void input_poll(void) {
    if (info.i8042) {
        poll_i8042();
    }
    for (int i = 0; i < nusb; i++) {
        if (usb[i].live) {
            poll_usb(i);
        }
    }
    if (info.conin) {
        poll_conin();
    }
    for (int i = 0; i < nfw; i++) {
        int b, dx, dy;
        if (fwptr_read(fw_ids[i], &b, &dx, &dy) > 0) {
            mouse_add(MSRC_FW0 + i, b, dx, dy);
        }
    }
}

bool input_next_key(key_event *ev) {
    if (q.head == q.tail) {
        input_poll();
    }
    if (q.head == q.tail) {
        return false;
    }
    *ev = q.ev[q.tail++ % QUEUE];
    return true;
}

bool input_mouse(int *buttons, int *dx, int *dy) {
    bool changed = mouse_changed;
    *buttons = mouse_buttons;
    *dx = mouse_dx;
    *dy = mouse_dy;
    mouse_dx = mouse_dy = 0;
    mouse_changed = false;
    return changed;
}

bool input_wait_any_key(uint32_t timeout_ms) {
    if (!ready) {
        return conin_wait_key(timeout_ms);
    }
    uint64_t end = clock_us() + (uint64_t)timeout_ms * 1000;
    for (;;) {
        key_event ev;
        while (input_next_key(&ev)) {
            if (ev.pressed) {
                return true;
            }
        }
        uint64_t now = clock_us();
        if (timeout_ms && now >= end) {
            return false;
        }
        idle_until(now + 10000);
    }
}

const input_info *input_get_info(void) {
    return &info;
}
