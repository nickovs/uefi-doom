/* UEFI console input: capability probe, non-blocking reads, wait for a key. */
#include "efi/efi.h"

#include <Protocol/SimpleTextInEx.h>

#include "efi/log.h"
#include "input/input_hw.h"

EFI_GUID gEfiSimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *ex;

void conin_probe(conin_info *out) {
    out->has_ex = !EFI_ERROR(gBS->HandleProtocol(
                      gST->ConsoleInHandle, &gEfiSimpleTextInputExProtocolGuid, (void **)&ex)) &&
                  ex;
    out->exposed_ok = false;
    out->exposed_status = EFI_UNSUPPORTED;
    if (out->has_ex) {
        EFI_KEY_TOGGLE_STATE ts = EFI_TOGGLE_STATE_VALID | EFI_KEY_STATE_EXPOSED;
        EFI_STATUS st = ex->SetState(ex, &ts);
        out->exposed_status = st;
        out->exposed_ok = !EFI_ERROR(st);
    } else {
        ex = NULL;
    }
}

bool conin_read(uint16_t *scan, uint16_t *ch, uint32_t *shift_state) {
    if (ex) {
        EFI_KEY_DATA kd;
        if (EFI_ERROR(ex->ReadKeyStrokeEx(ex, &kd))) {
            return false;
        }
        *scan = kd.Key.ScanCode;
        *ch = kd.Key.UnicodeChar;
        *shift_state = kd.KeyState.KeyShiftState;
        return true;
    }
    EFI_INPUT_KEY k;
    if (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &k))) {
        return false;
    }
    *scan = k.ScanCode;
    *ch = k.UnicodeChar;
    *shift_state = 0;
    return true;
}

bool conin_wait_key(uint32_t timeout_ms) {
    EFI_EVENT events[2] = {gST->ConIn->WaitForKey, NULL};
    UINTN n = 1, which;
    if (timeout_ms) {
        if (EFI_ERROR(gBS->CreateEvent(EVT_TIMER, 0, NULL, NULL, &events[1]))) {
            return false;
        }
        gBS->SetTimer(events[1], TimerRelative, (UINT64)timeout_ms * 10000);
        n = 2;
    }
    EFI_STATUS st = gBS->WaitForEvent(n, events, &which);
    if (events[1]) {
        gBS->CloseEvent(events[1]);
    }
    return !EFI_ERROR(st) && which == 0;
}
