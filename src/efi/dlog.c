/* DoomLog variable writer. See docs/architecture.md#logging. */
#include "efi/dlog.h"

#include <stdio.h>
#include <string.h>

#include "efi/dlog_core.h"
#include "efi/efi.h"

#define DLOG_ATTRS (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

static EFI_GUID dlog_guid = DLOG_GUID_INIT;
static uint8_t chunk_buf[DLOG_CHUNK_CAP];

static void report(dlog_err_fn err, const char *what, EFI_STATUS st) {
    char msg[128];
    int n = snprintf(msg, sizeof msg, "dlog: %s: %s\n", what, efi_status_str(st));
    if (err && n > 0) {
        err(msg, strnlen(msg, sizeof msg));
    }
}

static void delete_all(void) {
    CHAR16 name[11];
    gRT->SetVariable(L"DoomLogInfo", &dlog_guid, 0, 0, NULL);
    for (unsigned i = 0; i < DLOG_MAX_CHUNKS; i++) {
        dlog_chunk_name(i, (uint16_t *)name);
        if (gRT->SetVariable(name, &dlog_guid, 0, 0, NULL) == EFI_NOT_FOUND) {
            break;
        }
    }
}

/* Copy `len` bytes starting at logical offset `off` of the span list. */
static void gather(const log_span *spans, size_t nspans, size_t off, size_t len, uint8_t *dst) {
    for (size_t i = 0; i < nspans && len; i++) {
        if (off >= spans[i].n) {
            off -= spans[i].n;
            continue;
        }
        size_t take = spans[i].n - off < len ? spans[i].n - off : len;
        memcpy(dst, spans[i].p + off, take);
        dst += take;
        len -= take;
        off = 0;
    }
}

/* Write chunks of `chunk` bytes; returns chunks written, or -1 if the first
 * write failed with a size-related status (caller retries smaller). */
static int write_chunks(const log_span *spans, size_t nspans, size_t stored, size_t chunk,
                        size_t *written_bytes, dlog_err_fn err) {
    int count = 0;
    CHAR16 name[11];
    *written_bytes = 0;
    for (size_t off = 0; off < stored && count < DLOG_MAX_CHUNKS; off += chunk) {
        size_t len = stored - off < chunk ? stored - off : chunk;
        gather(spans, nspans, off, len, chunk_buf);
        dlog_chunk_name((unsigned)count, (uint16_t *)name);
        EFI_STATUS st = gRT->SetVariable(name, &dlog_guid, DLOG_ATTRS, len, chunk_buf);
        if (EFI_ERROR(st)) {
            if (count == 0 && (st == EFI_OUT_OF_RESOURCES || st == EFI_INVALID_PARAMETER)) {
                return -1;
            }
            report(err, "chunk write stopped", st);
            break;
        }
        count++;
        *written_bytes += len;
    }
    return count;
}

void dlog_write(const log_ring *ring, uint16_t exit_reason, uint64_t exit_status,
                uint64_t run_time_us, const char *build_id, dlog_err_fn err) {
    UINT64 max_storage = 0, remaining = 0, max_var = 0;
    EFI_STATUS st = EFI_UNSUPPORTED;
    if (gRT->Hdr.Revision >= EFI_2_00_SYSTEM_TABLE_REVISION) {
        st = gRT->QueryVariableInfo(DLOG_ATTRS, &max_storage, &remaining, &max_var);
    }
    if (EFI_ERROR(st)) {
        report(err, "QueryVariableInfo", st);
        remaining = max_var = 0;
    }
    size_t chunk, budget;
    dlog_size_plan(max_var, remaining, &chunk, &budget);

    delete_all();

    log_span spans[3];
    uint64_t dropped;
    size_t nspans = ring_snapshot(ring, budget, spans, &dropped);
    size_t stored = 0;
    for (size_t i = 0; i < nspans; i++) {
        stored += spans[i].n;
    }

    int count;
    size_t written;
    while ((count = write_chunks(spans, nspans, stored, chunk, &written, err)) < 0) {
        if (chunk / 2 < DLOG_CHUNK_MIN) {
            report(err, "no chunk size accepted", EFI_OUT_OF_RESOURCES);
            count = 0;
            written = 0;
            break;
        }
        chunk /= 2;
    }

    /* The head is the first span when the log started inside the head area. */
    size_t head = ring->total && ring->head_cap ? spans[0].n : 0;
    if (head > ring->head_cap) {
        head = 0;
    }
    if (head > written) {
        head = written;
    }
    dlog_info info;
    dlog_info_fill(&info, exit_reason, exit_status, (uint32_t)count, (uint32_t)chunk, written,
                   dropped + (stored - written), head, run_time_us, build_id);
    st = gRT->SetVariable(L"DoomLogInfo", &dlog_guid, DLOG_ATTRS, sizeof info, &info);
    if (EFI_ERROR(st)) {
        report(err, "DoomLogInfo write", st);
    }
}
