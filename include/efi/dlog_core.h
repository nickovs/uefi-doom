/* Format of the volatile "DoomLog" UEFI variables (devloop profile).
 * See docs/architecture.md#logging. Mirrored by tools/dlog.py.
 * Pure code: no UEFI dependencies, unit-tested on the host. */
#ifndef UD_EFI_DLOG_CORE_H
#define UD_EFI_DLOG_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Vendor GUID 89c5fe58-f223-4f55-a8dc-6e4cfee8bf6a */
#define DLOG_GUID_INIT                                                                             \
    {0x89c5fe58, 0xf223, 0x4f55, {0xa8, 0xdc, 0x6e, 0x4c, 0xfe, 0xe8, 0xbf, 0x6a}}

#define DLOG_VERSION 1
#define DLOG_MAX_CHUNKS 1000          /* DoomLog000 .. DoomLog999 */
#define DLOG_CHUNK_CAP (32u * 1024)   /* largest chunk we write */
#define DLOG_CHUNK_MIN 1024u          /* smallest chunk we try */
#define DLOG_BUDGET_CAP (256u * 1024) /* largest total log we store */
#define DLOG_VAR_OVERHEAD 128u        /* name + firmware header, conservatively */

/** Little-endian, packed header stored in the DoomLogInfo variable. */
typedef struct __attribute__((packed)) {
    char magic[4];          /* "DLOG" */
    uint16_t version;       /* DLOG_VERSION */
    uint16_t exit_reason;   /* exit_reason enum (efi_main.h) */
    uint64_t exit_status;   /* EFI_STATUS returned to firmware */
    uint32_t chunk_count;   /* DoomLog000 .. DoomLog(count-1) */
    uint32_t chunk_size;    /* size of every chunk except possibly the last */
    uint64_t stored_bytes;  /* log bytes across all chunks */
    uint64_t dropped_bytes; /* bytes left out, between the head and the tail */
    uint64_t head_bytes;    /* stored bytes before the gap (the preserved head) */
    uint64_t run_time_us;   /* time from entry to exit */
    char build_id[64];      /* NUL-padded */
} dlog_info;

_Static_assert(sizeof(dlog_info) == 120, "dlog_info layout is part of the variable format");

/** Write the UCS-2 variable name for chunk `idx` (e.g. L"DoomLog007") into `name`. */
void dlog_chunk_name(unsigned idx, uint16_t name[11]);

/**
 * Choose chunk size and total budget from QueryVariableInfo results.
 * `max_var` or `remaining` of 0 means "unknown".
 */
void dlog_size_plan(uint64_t max_var, uint64_t remaining, size_t *chunk, size_t *budget);

/** Fill `info` (magic, version, sizes, build id); zero-pads unused bytes. */
void dlog_info_fill(dlog_info *info, uint16_t exit_reason, uint64_t exit_status,
                    uint32_t chunk_count, uint32_t chunk_size, uint64_t stored, uint64_t dropped,
                    uint64_t head, uint64_t run_time_us, const char *build_id);

#endif
