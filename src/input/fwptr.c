/* Firmware pointers: EFI_SIMPLE_POINTER_PROTOCOL and EFI_ABSOLUTE_POINTER_PROTOCOL.
 * Whatever drives the firmware's own graphical setup (often a touchpad we could
 * not otherwise drive, such as I2C-HID) is read here as relative motion. */
#include <stdio.h>
#include <string.h>

#include "efi/efi.h"

#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimplePointer.h>

#include "efi/log.h"
#include "input/input_hw.h"
#include "input/pointer_core.h"

EFI_GUID gEfiSimplePointerProtocolGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
EFI_GUID gEfiAbsolutePointerProtocolGuid = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;

static struct {
    fwptr_desc desc;
    EFI_SIMPLE_POINTER_PROTOCOL *sp;
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap;
    absptr_track track;
} ptrs[FWPTR_MAX];
static int nptrs;

/* Short device-path summary: ACPI HIDs, USB ports, else type/subtype. */
static void summarise(EFI_HANDLE h, char *out, size_t cap) {
    EFI_DEVICE_PATH_PROTOCOL *dp = NULL;
    size_t k = 0;
    out[0] = 0;
    if (EFI_ERROR(gBS->HandleProtocol(h, &gEfiDevicePathProtocolGuid, (void **)&dp)) || !dp) {
        snprintf(out, cap, "(no device path)");
        return;
    }
    for (int guard = 0; guard < 16 && k + 16 < cap; guard++) {
        size_t len = (size_t)dp->Length[0] | (size_t)dp->Length[1] << 8;
        if (dp->Type == END_DEVICE_PATH_TYPE || len < 4) {
            break;
        }
        if (dp->Type == ACPI_DEVICE_PATH && dp->SubType == ACPI_DP &&
            len >= sizeof(ACPI_HID_DEVICE_PATH)) {
            UINT32 hid = ((ACPI_HID_DEVICE_PATH *)dp)->HID;
            k += (size_t)snprintf(out + k, cap - k, "/ACPI(%c%c%c%04x)", '@' + (hid >> 10 & 31),
                                  '@' + (hid >> 5 & 31), '@' + (hid & 31), (unsigned)(hid >> 16));
        } else if (dp->Type == MESSAGING_DEVICE_PATH && dp->SubType == MSG_USB_DP && len >= 6) {
            k += (size_t)snprintf(out + k, cap - k, "/USB(%u)", ((uint8_t *)dp)[4]);
        } else {
            k += (size_t)snprintf(out + k, cap - k, "/%u.%u", dp->Type, dp->SubType);
        }
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((uint8_t *)dp + len);
    }
}

static void add_handles(EFI_GUID *guid, bool absolute) {
    UINTN n = 0;
    EFI_HANDLE *hs = NULL;
    if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, guid, NULL, &n, &hs))) {
        return;
    }
    for (UINTN i = 0; i < n && nptrs < FWPTR_MAX; i++) {
        void *proto;
        if (EFI_ERROR(gBS->HandleProtocol(hs[i], guid, &proto)) || !proto) {
            continue;
        }
        memset(&ptrs[nptrs], 0, sizeof ptrs[nptrs]);
        fwptr_desc *d = &ptrs[nptrs].desc;
        d->absolute = absolute;
        d->console = hs[i] == gST->ConsoleInHandle;
        summarise(hs[i], d->path, sizeof d->path);
        if (absolute) {
            EFI_ABSOLUTE_POINTER_PROTOCOL *ap = proto;
            if (!ap->Mode) {
                continue;
            }
            ptrs[nptrs].ap = ap;
            d->min_x = ap->Mode->AbsoluteMinX;
            d->max_x = ap->Mode->AbsoluteMaxX;
            d->min_y = ap->Mode->AbsoluteMinY;
            d->max_y = ap->Mode->AbsoluteMaxY;
            d->attributes = ap->Mode->Attributes;
            ap->Reset(ap, FALSE);
        } else {
            EFI_SIMPLE_POINTER_PROTOCOL *sp = proto;
            if (!sp->Mode) {
                continue;
            }
            ptrs[nptrs].sp = sp;
            d->res_x = sp->Mode->ResolutionX;
            d->res_y = sp->Mode->ResolutionY;
            d->buttons = (sp->Mode->LeftButton ? 1 : 0) | (sp->Mode->RightButton ? 2 : 0);
            sp->Reset(sp, FALSE);
        }
        nptrs++;
    }
    gBS->FreePool(hs);
}

int fwptr_enumerate(void) {
    nptrs = 0;
    add_handles(&gEfiSimplePointerProtocolGuid, false);
    add_handles(&gEfiAbsolutePointerProtocolGuid, true);
    return nptrs;
}

const fwptr_desc *fwptr_describe(int i) {
    return &ptrs[i].desc;
}

int fwptr_read(int i, int *buttons, int *dx, int *dy) {
    if (ptrs[i].sp) {
        EFI_SIMPLE_POINTER_STATE s;
        EFI_STATUS st = ptrs[i].sp->GetState(ptrs[i].sp, &s);
        if (st == EFI_NOT_READY) {
            return 0;
        }
        if (EFI_ERROR(st)) {
            return -1;
        }
        *buttons = (s.LeftButton ? 1 : 0) | (s.RightButton ? 2 : 0);
        *dx = pointer_scale_simple(s.RelativeMovementX, ptrs[i].desc.res_x);
        *dy = -pointer_scale_simple(s.RelativeMovementY, ptrs[i].desc.res_y); /* +Y is down */
        return 1;
    }
    EFI_ABSOLUTE_POINTER_STATE s;
    EFI_STATUS st = ptrs[i].ap->GetState(ptrs[i].ap, &s);
    if (st == EFI_NOT_READY) {
        return 0;
    }
    if (EFI_ERROR(st)) {
        return -1;
    }
    const fwptr_desc *d = &ptrs[i].desc;
    absptr_motion(&ptrs[i].track, s.ActiveButtons & EFI_ABSP_TouchActive, s.CurrentX, s.CurrentY,
                  d->min_x, d->max_x, d->min_y, d->max_y, dx, dy);
    /* Touch is how the pointer moves, so it is not a button; the alternate
     * button (e.g. a pen's barrel button or a pad's click) fires. */
    *buttons = (s.ActiveButtons & EFI_ABS_AltActive) ? 1 : 0;
    return 1;
}
