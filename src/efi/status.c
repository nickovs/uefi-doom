/* EFI_STATUS names for logs. */
#include <stdio.h>

#include "efi/efi.h"

const char *efi_status_str(EFI_STATUS status) {
    static const char *const errors[] = {
        "EFI_SUCCESS",          "EFI_LOAD_ERROR",           "EFI_INVALID_PARAMETER",
        "EFI_UNSUPPORTED",      "EFI_BAD_BUFFER_SIZE",      "EFI_BUFFER_TOO_SMALL",
        "EFI_NOT_READY",        "EFI_DEVICE_ERROR",         "EFI_WRITE_PROTECTED",
        "EFI_OUT_OF_RESOURCES", "EFI_VOLUME_CORRUPTED",     "EFI_VOLUME_FULL",
        "EFI_NO_MEDIA",         "EFI_MEDIA_CHANGED",        "EFI_NOT_FOUND",
        "EFI_ACCESS_DENIED",    "EFI_NO_RESPONSE",          "EFI_NO_MAPPING",
        "EFI_TIMEOUT",          "EFI_NOT_STARTED",          "EFI_ALREADY_STARTED",
        "EFI_ABORTED",          "EFI_ICMP_ERROR",           "EFI_TFTP_ERROR",
        "EFI_PROTOCOL_ERROR",   "EFI_INCOMPATIBLE_VERSION", "EFI_SECURITY_VIOLATION",
        "EFI_CRC_ERROR",        "EFI_END_OF_MEDIA",
    };
    static char other[32];
    UINTN code = status & ~MAX_BIT;
    if (status == EFI_SUCCESS) {
        return errors[0];
    }
    if (EFI_ERROR(status) && code < sizeof errors / sizeof errors[0]) {
        return errors[code];
    }
    snprintf(other, sizeof other, EFI_ERROR(status) ? "ERROR(%llu)" : "WARN(%llu)",
             (unsigned long long)code);
    return other;
}
