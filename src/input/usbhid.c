/* USB HID boot keyboards/mice: discovery, takeover and async report capture. */
#include <stdlib.h>
#include <string.h>

#include "efi/efi.h"

#include <Protocol/DriverBinding.h>
#include <Protocol/UsbIo.h>

#include "efi/clock.h"
#include "efi/efi_main.h"
#include "efi/log.h"
#include "input/input_hw.h"

EFI_GUID gEfiUsbIoProtocolGuid = EFI_USB_IO_PROTOCOL_GUID;
EFI_GUID gEfiDriverBindingProtocolGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID;

#define RING 16 /* power of two */
#define HID_SET_IDLE 0x0a
#define HID_SET_PROTOCOL 0x0b

struct usbhid_dev {
    EFI_HANDLE handle;
    EFI_USB_IO_PROTOCOL *io;
    usbhid_desc desc;
    bool running;
    bool owned;         /* exclusive open held */
    volatile bool lost; /* firmware stopped us: device unplugged */
    /* Single producer (callback at raised TPL) / single consumer (main loop). */
    volatile uint32_t head, tail;
    uint8_t reports[RING][8];
    uint8_t lens[RING];
    uint64_t times[RING];
    volatile uint32_t dropped, errors;
};

static usbhid_dev devs[USBHID_MAX_DEVS];

int usbhid_enumerate(usbhid_dev **out, int max) {
    UINTN n = 0;
    EFI_HANDLE *hs = NULL;
    if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &n, &hs))) {
        return 0;
    }
    int found = 0;
    for (UINTN i = 0; i < n && found < max && found < USBHID_MAX_DEVS; i++) {
        EFI_USB_IO_PROTOCOL *io;
        EFI_USB_INTERFACE_DESCRIPTOR ifd;
        EFI_USB_DEVICE_DESCRIPTOR dd;
        if (EFI_ERROR(gBS->HandleProtocol(hs[i], &gEfiUsbIoProtocolGuid, (void **)&io)) ||
            EFI_ERROR(io->UsbGetInterfaceDescriptor(io, &ifd)) ||
            EFI_ERROR(io->UsbGetDeviceDescriptor(io, &dd))) {
            continue;
        }
        if (ifd.InterfaceClass != 3 || ifd.InterfaceSubClass != 1 ||
            (ifd.InterfaceProtocol != 1 && ifd.InterfaceProtocol != 2)) {
            continue;
        }
        usbhid_dev *d = &devs[found];
        memset(d, 0, sizeof *d);
        for (UINT8 e = 0; e < ifd.NumEndpoints; e++) {
            EFI_USB_ENDPOINT_DESCRIPTOR ep;
            if (!EFI_ERROR(io->UsbGetEndpointDescriptor(io, e, &ep)) &&
                (ep.Attributes & 3) == 3 /* interrupt */ && (ep.EndpointAddress & 0x80) &&
                ep.MaxPacketSize >= 3 && ep.MaxPacketSize <= 64) {
                d->desc.endpoint = ep.EndpointAddress;
                d->desc.max_packet = ep.MaxPacketSize;
                d->desc.interval_ms = ep.Interval ? ep.Interval : 1;
                break;
            }
        }
        if (!d->desc.endpoint) {
            log_msg(LOG_WARN, "usbhid: %04x:%04x has no usable interrupt-IN endpoint", dd.IdVendor,
                    dd.IdProduct);
            continue;
        }
        d->handle = hs[i];
        d->io = io;
        d->desc.vid = dd.IdVendor;
        d->desc.pid = dd.IdProduct;
        d->desc.protocol = ifd.InterfaceProtocol;
        d->desc.iface = ifd.InterfaceNumber;
        out[found++] = d;
    }
    gBS->FreePool(hs);
    return found;
}

const usbhid_desc *usbhid_describe(const usbhid_dev *d) {
    return &d->desc;
}

static EFI_STATUS EFIAPI on_report(void *data, UINTN len, void *ctx, UINT32 status) {
    usbhid_dev *d = ctx;
    if (status != EFI_USB_NOERROR) {
        d->errors++;
        return EFI_SUCCESS;
    }
    uint32_t h = d->head;
    if (h - d->tail >= RING) {
        d->dropped++;
        return EFI_SUCCESS;
    }
    size_t n = len < 8 ? len : 8;
    memcpy(d->reports[h % RING], data, n);
    d->lens[h % RING] = (uint8_t)n;
    d->times[h % RING] = clock_us();
    d->head = h + 1;
    return EFI_SUCCESS;
}

/* Devices are opened BY_DRIVER, as a driver would, so that when one is
 * unplugged the USB bus driver can take it away: it calls our binding's Stop()
 * instead of failing to disconnect (and retrying forever). The binding never
 * starts on anything itself. */
static EFI_STATUS EFIAPI binding_supported(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
                                           EFI_DEVICE_PATH_PROTOCOL *Remaining) {
    (void)This;
    (void)Controller;
    (void)Remaining;
    return EFI_UNSUPPORTED;
}

static EFI_STATUS EFIAPI binding_start(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
                                       EFI_DEVICE_PATH_PROTOCOL *Remaining) {
    (void)This;
    (void)Controller;
    (void)Remaining;
    return EFI_UNSUPPORTED;
}

/* Runs at TPL_CALLBACK from the bus driver: only release the device and set
 * flags (the main loop notices `lost`). */
static EFI_STATUS EFIAPI binding_stop(EFI_DRIVER_BINDING_PROTOCOL *This, EFI_HANDLE Controller,
                                      UINTN NumberOfChildren, EFI_HANDLE *Children) {
    (void)This;
    (void)NumberOfChildren;
    (void)Children;
    for (int i = 0; i < USBHID_MAX_DEVS; i++) {
        usbhid_dev *d = &devs[i];
        if (d->owned && d->handle == Controller) {
            if (d->running) {
                d->io->UsbAsyncInterruptTransfer(d->io, d->desc.endpoint, FALSE, 0, 0, NULL, NULL);
                d->running = false;
            }
            gBS->CloseProtocol(d->handle, &gEfiUsbIoProtocolGuid, gImage, d->handle);
            d->owned = false;
            d->lost = true;
        }
    }
    return EFI_SUCCESS;
}

static EFI_DRIVER_BINDING_PROTOCOL binding = {
    binding_supported, binding_start, binding_stop, 0x10, NULL, NULL};
static bool binding_installed;

static void binding_cleanup(void *ctx) {
    (void)ctx;
    EFI_STATUS st =
        gBS->UninstallProtocolInterface(gImage, &gEfiDriverBindingProtocolGuid, &binding);
    if (EFI_ERROR(st)) {
        log_msg(LOG_WARN, "usbhid: uninstall driver binding: %s", efi_status_str(st));
    }
    binding_installed = false;
}

static bool install_binding(void) {
    if (binding_installed) {
        return true;
    }
    binding.ImageHandle = gImage;
    binding.DriverBindingHandle = gImage;
    EFI_HANDLE h = gImage;
    EFI_STATUS st = gBS->InstallProtocolInterface(&h, &gEfiDriverBindingProtocolGuid,
                                                  EFI_NATIVE_INTERFACE, &binding);
    if (EFI_ERROR(st)) {
        log_msg(LOG_WARN, "usbhid: install driver binding: %s", efi_status_str(st));
        return false;
    }
    binding_installed = true;
    efi_cleanup_push(binding_cleanup, NULL, "usb binding"); /* runs after device cleanups */
    return true;
}

bool usbhid_lost(const usbhid_dev *d) {
    return d->lost;
}

void usbhid_stop(usbhid_dev *d) {
    if (!d->owned) {
        return;
    }
    d->owned = false;
    if (d->running) {
        d->io->UsbAsyncInterruptTransfer(d->io, d->desc.endpoint, FALSE, 0, 0, NULL, NULL);
        d->running = false;
    }
    gBS->CloseProtocol(d->handle, &gEfiUsbIoProtocolGuid, gImage, d->handle);
    EFI_STATUS st = gBS->ConnectController(d->handle, NULL, NULL, TRUE);
    log_msg(EFI_ERROR(st) ? LOG_WARN : LOG_INFO, "usbhid: %04x:%04x returned to firmware: %s",
            d->desc.vid, d->desc.pid, efi_status_str(st));
}

static void stop_cleanup(void *ctx) {
    usbhid_stop(ctx);
}

static EFI_STATUS class_request(usbhid_dev *d, UINT8 request, UINT16 value) {
    EFI_USB_DEVICE_REQUEST req = {
        .RequestType = 0x21, /* host-to-device, class, interface */
        .Request = request,
        .Value = value,
        .Index = d->desc.iface,
        .Length = 0,
    };
    UINT32 usb_status = 0;
    return d->io->UsbControlTransfer(d->io, &req, EfiUsbNoData, 1000, NULL, 0, &usb_status);
}

bool usbhid_start(usbhid_dev *d) {
    EFI_USB_IO_PROTOCOL *io;
    if (!install_binding()) {
        return false;
    }
    EFI_STATUS st =
        gBS->OpenProtocol(d->handle, &gEfiUsbIoProtocolGuid, (void **)&io, gImage, d->handle,
                          EFI_OPEN_PROTOCOL_BY_DRIVER | EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if (EFI_ERROR(st)) {
        log_msg(LOG_WARN, "usbhid: %04x:%04x exclusive open failed: %s", d->desc.vid, d->desc.pid,
                efi_status_str(st));
        return false;
    }
    d->io = io;
    d->owned = true;
    efi_cleanup_push(stop_cleanup, d, "usbhid");

    st = class_request(d, HID_SET_PROTOCOL, 0 /* boot */);
    if (EFI_ERROR(st)) {
        log_msg(LOG_WARN, "usbhid: SET_PROTOCOL(boot): %s", efi_status_str(st));
    }
    st = class_request(d, HID_SET_IDLE, 0); /* optional request: a STALL is fine */
    if (EFI_ERROR(st)) {
        log_msg(LOG_INFO, "usbhid: SET_IDLE(0): %s (ignored)", efi_status_str(st));
    }
    st = io->UsbAsyncInterruptTransfer(io, d->desc.endpoint, TRUE, d->desc.interval_ms,
                                       d->desc.max_packet, on_report, d);
    if (EFI_ERROR(st)) {
        log_msg(LOG_WARN, "usbhid: async transfer failed: %s", efi_status_str(st));
        return false;
    }
    d->running = true;
    return true;
}

bool usbhid_next_report(usbhid_dev *d, uint8_t report[8], size_t *len, uint64_t *t_us) {
    uint32_t t = d->tail;
    if (t == d->head) {
        return false;
    }
    memcpy(report, d->reports[t % RING], 8);
    *len = d->lens[t % RING];
    *t_us = d->times[t % RING];
    d->tail = t + 1;
    return true;
}

void usbhid_stats(const usbhid_dev *d, uint32_t *dropped, uint32_t *errors) {
    *dropped = d->dropped;
    *errors = d->errors;
}
