/* HWPROBE input section: which keyboard paths exist, whether we can take them
 * over and give them back, and (interactively) what they deliver. */
#include <stdio.h>
#include <string.h>

#include "efi/acpi.h"
#include "efi/clock.h"
#include "efi/log.h"
#include "hwprobe.h"
#include "input/input_hw.h"

#define CAPTURE_US 6000000

static void conin_section(void) {
    conin_info ci;
    conin_probe(&ci);
    log_msg(LOG_INFO, "ConIn: SimpleTextInputEx %s; EFI_KEY_STATE_EXPOSED %s (0x%llx)",
            ci.has_ex ? "yes" : "no", ci.exposed_ok ? "accepted" : "refused",
            (unsigned long long)ci.exposed_status);
    rep_obj("conin");
    rep_bool("text_input_ex", ci.has_ex);
    rep_bool("key_state_exposed", ci.exposed_ok);
    if (!hw_batch) {
        /* Typematic timing: how long until auto-repeat starts, and its rate. */
        uint16_t s, c;
        uint32_t sh;
        while (conin_read(&s, &c, &sh)) {
        }
        uint64_t t[64];
        int n = 0;
        if (hw_wait_enter("key repeat. After ENTER, press and HOLD the A key for 3 seconds.",
                          120000) &&
            conin_wait_key(30000)) {
            uint64_t end = clock_us() + 3500000;
            while (clock_us() < end && n < 64) {
                if (conin_read(&s, &c, &sh)) {
                    t[n++] = clock_us();
                }
            }
        }
        if (n >= 3) {
            uint64_t gaps[63];
            for (int i = 2; i < n; i++) {
                gaps[i - 2] = t[i] - t[i - 1];
            }
            uint64_t rep = hw_median(gaps, n - 2);
            log_msg(LOG_INFO, "ConIn: %d strokes; first repeat after %llu ms; repeat every %llu ms",
                    n, (unsigned long long)((t[1] - t[0]) / 1000),
                    (unsigned long long)(rep / 1000));
            rep_int("strokes", n);
            rep_int("repeat_delay_ms", (int64_t)((t[1] - t[0]) / 1000));
            rep_int("repeat_interval_ms", (int64_t)(rep / 1000));
        } else {
            log_msg(LOG_INFO, "ConIn: only %d strokes seen (no auto-repeat measured)", n);
            rep_int("strokes", n);
        }
    }
    rep_end();
}

static void i8042_section(void) {
    acpi_fadt_info f;
    bool fadt_flag = acpi_fadt(&f) && (f.iapc_boot_arch & ACPI_IAPC_8042);
    bool present = i8042_present();
    int handles = i8042_firmware_handles();
    log_msg(LOG_INFO, "i8042: present=%d FADT flag=%d firmware PS/2 keyboard handles=%d", present,
            fadt_flag, handles);
    rep_obj("i8042");
    rep_bool("present", present);
    rep_bool("fadt_8042_flag", fadt_flag);
    rep_int("firmware_handles", handles);
    if (!present || !handles) {
        rep_end();
        return;
    }
    /* Ask before taking over: ConIn (and so ENTER) stops working once we own the port. */
    bool capture = hw_wait_enter("internal keyboard capture. After ENTER: press and release LEFT "
                                 "CTRL, hold UP ARROW for 1 s, then move the touchpad.",
                                 120000);
    bool took = i8042_take_over();
    rep_bool("takeover", took);
    if (took) {
        uint8_t cmd;
        if (i8042_read_command_byte(&cmd)) {
            log_msg(LOG_INFO, "i8042: command byte 0x%02x (translation %s)", cmd,
                    cmd & 0x40 ? "on" : "off");
            rep_hex("command_byte", cmd);
            rep_bool("translation", cmd & 0x40);
        } else {
            log_msg(LOG_WARN, "i8042: command byte read timed out");
            rep_bool("command_byte_timeout", true);
        }
        int aux_id = i8042_aux_start();
        rep_int("aux_device_id", aux_id);
        if (capture) {
            log_msg(LOG_INFO, ">>> CAPTURING NOW (6 s): press and release LEFT CTRL, hold UP "
                              "ARROW for 1 s, then move the touchpad.");
            char hex[3 * 128 + 1] = "";
            int total = 0, aux = 0;
            uint64_t end = clock_us() + CAPTURE_US;
            while (clock_us() < end) {
                uint8_t b[16];
                bool a[16];
                int n = i8042_poll(b, a, 16);
                for (int i = 0; i < n; i++) {
                    if (a[i]) {
                        aux++;
                        continue;
                    }
                    if (total < 128) {
                        snprintf(hex + 3 * total, 4, "%02x ", b[i]);
                    }
                    total++;
                }
            }
            log_msg(LOG_INFO, "i8042: %d keyboard bytes, %d aux bytes: %s", total, aux, hex);
            rep_int("captured_bytes", total);
            rep_int("aux_bytes", aux);
            rep_str("bytes", hex);
        }
        int back = i8042_restore();
        rep_int("restored_handles", back);
        rep_int("firmware_handles_after", i8042_firmware_handles());
    }
    rep_end();
}

static void usb_section(void) {
    usbhid_dev *devs[USBHID_MAX_DEVS];
    int n = usbhid_enumerate(devs, USBHID_MAX_DEVS);
    log_msg(LOG_INFO, "USB HID boot devices: %d", n);
    rep_arr("usb_hid");
    for (int i = 0; i < n; i++) {
        const usbhid_desc *d = usbhid_describe(devs[i]);
        log_msg(LOG_INFO, "  %04x:%04x %s iface %u ep 0x%02x interval %u ms maxpkt %u", d->vid,
                d->pid, d->protocol == 1 ? "keyboard" : "mouse", d->iface, d->endpoint,
                d->interval_ms, d->max_packet);
        rep_obj(NULL);
        rep_hex("vid", d->vid);
        rep_hex("pid", d->pid);
        rep_str("kind", d->protocol == 1 ? "keyboard" : "mouse");
        rep_int("interval_ms", d->interval_ms);
        rep_int("max_packet", d->max_packet);
        bool capture = hw_wait_enter("USB device capture. After ENTER: press and release keys on "
                                     "it (or move it) for 6 s.",
                                     120000);
        bool ok = usbhid_start(devs[i]);
        rep_bool("takeover", ok);
        if (ok) {
            uint64_t window = capture ? CAPTURE_US : 300000;
            if (capture) {
                log_msg(LOG_INFO, ">>> CAPTURING NOW (6 s): use %04x:%04x.", d->vid, d->pid);
            }
            int reports = 0;
            uint64_t first = 0, last = 0, end = clock_us() + window;
            while (clock_us() < end) {
                uint8_t r[8];
                size_t len;
                uint64_t t;
                while (usbhid_next_report(devs[i], r, &len, &t)) {
                    if (reports < 6) {
                        log_msg(LOG_INFO,
                                "    report %zu bytes: %02x %02x %02x %02x %02x %02x "
                                "%02x %02x",
                                len, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
                    }
                    first = reports ? first : t;
                    last = t;
                    reports++;
                }
            }
            uint32_t dropped, errors;
            usbhid_stats(devs[i], &dropped, &errors);
            log_msg(LOG_INFO, "    %d reports, %u dropped, %u errors", reports, dropped, errors);
            rep_int("reports", reports);
            rep_int("dropped", dropped);
            rep_int("errors", errors);
            if (reports > 1) {
                rep_int("mean_report_gap_us", (int64_t)((last - first) / (uint64_t)(reports - 1)));
            }
            usbhid_stop(devs[i]);
        }
        rep_end();
    }
    rep_end();
}

/* Firmware pointer protocols: what the firmware's own graphical setup uses. */
static void pointer_section(void) {
    int n = fwptr_enumerate();
    log_msg(LOG_INFO, "firmware pointers: %d", n);
    rep_arr("firmware_pointers");
    for (int i = 0; i < n; i++) {
        const fwptr_desc *d = fwptr_describe(i);
        rep_obj(NULL);
        rep_str("kind", d->absolute ? "absolute" : "simple");
        rep_str("path", d->path);
        rep_bool("console", d->console);
        if (d->absolute) {
            log_msg(LOG_INFO, "  %d: ABSOLUTE_POINTER%s %s x %llu..%llu y %llu..%llu attr 0x%x", i,
                    d->console ? " (console)" : "", d->path, (unsigned long long)d->min_x,
                    (unsigned long long)d->max_x, (unsigned long long)d->min_y,
                    (unsigned long long)d->max_y, d->attributes);
            rep_int("min_x", (int64_t)d->min_x);
            rep_int("max_x", (int64_t)d->max_x);
            rep_int("min_y", (int64_t)d->min_y);
            rep_int("max_y", (int64_t)d->max_y);
            rep_int("attributes", d->attributes);
        } else {
            log_msg(LOG_INFO, "  %d: SIMPLE_POINTER%s %s resolution %llu x %llu /mm buttons 0x%x",
                    i, d->console ? " (console)" : "", d->path, (unsigned long long)d->res_x,
                    (unsigned long long)d->res_y, d->buttons);
            rep_int("res_x", (int64_t)d->res_x);
            rep_int("res_y", (int64_t)d->res_y);
            rep_int("buttons", d->buttons);
        }
        rep_end();
    }
    rep_end();
    if (n && hw_wait_enter("pointer capture. After ENTER: move the touchpad/mouse around and "
                           "click its buttons for 5 seconds.",
                           120000)) {
        rep_arr("pointer_capture");
        int states[FWPTR_MAX] = {0}, motion[FWPTR_MAX] = {0}, btn[FWPTR_MAX] = {0};
        uint64_t end = clock_us() + 5000000;
        while (clock_us() < end) {
            for (int i = 0; i < n; i++) {
                int b, dx, dy;
                if (fwptr_read(i, &b, &dx, &dy) > 0) {
                    states[i]++;
                    motion[i] += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                    btn[i] |= b;
                }
            }
            clock_sleep_us(2000);
        }
        for (int i = 0; i < n; i++) {
            log_msg(LOG_INFO, "  %d: %d states, motion %d, buttons seen 0x%x", i, states[i],
                    motion[i], btn[i]);
            rep_obj(NULL);
            rep_int("states", states[i]);
            rep_int("motion", motion[i]);
            rep_int("buttons", btn[i]);
            rep_end();
        }
        rep_end();
    }
}

void probe_input(void) {
    log_msg(LOG_INFO, "== input");
    rep_obj("input");
    conin_section();   /* before takeovers: needs the firmware keyboard drivers */
    pointer_section(); /* likewise the firmware mouse/touchpad drivers */
    i8042_section();
    usb_section();
    if (!hw_batch) {
        log_msg(LOG_INFO, ">>> All devices returned to firmware. Press any key to confirm the "
                          "keyboard works again (60 s).");
        bool ok = conin_wait_key(60000);
        uint16_t s, c;
        uint32_t sh;
        while (conin_read(&s, &c, &sh)) {
        }
        log_msg(ok ? LOG_INFO : LOG_WARN, "ConIn after restore: %s", ok ? "working" : "NO KEY");
        rep_bool("conin_after_restore", ok);
    }
    rep_end();
}
