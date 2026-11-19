/* Common UEFI include: EDK2 type/protocol definitions plus our globals. */
#ifndef UD_EFI_EFI_H
#define UD_EFI_EFI_H

/* <stddef.h> and EDK2's Base.h both define NULL (to the same value). */
#undef NULL
#include <Uefi.h>

extern EFI_HANDLE gImage;
extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;

/** Short name for an EFI_STATUS ("EFI_NOT_FOUND", "WARN(1)", ...). */
const char *efi_status_str(EFI_STATUS status);

#endif
