#include "efi/dlog_core.h"

#include <string.h>

void dlog_chunk_name(unsigned idx, uint16_t name[11]) {
    static const char prefix[] = "DoomLog";
    for (int i = 0; i < 7; i++) {
        name[i] = (uint16_t)prefix[i];
    }
    idx %= DLOG_MAX_CHUNKS;
    name[7] = (uint16_t)('0' + idx / 100);
    name[8] = (uint16_t)('0' + idx / 10 % 10);
    name[9] = (uint16_t)('0' + idx % 10);
    name[10] = 0;
}

void dlog_size_plan(uint64_t max_var, uint64_t remaining, size_t *chunk, size_t *budget) {
    size_t c = DLOG_CHUNK_CAP;
    if (max_var > DLOG_VAR_OVERHEAD && max_var - DLOG_VAR_OVERHEAD < c) {
        c = (size_t)(max_var - DLOG_VAR_OVERHEAD);
    }
    if (c < DLOG_CHUNK_MIN) {
        c = DLOG_CHUNK_MIN; /* firmware claims less; try anyway, writes will tell */
    }
    size_t b = DLOG_BUDGET_CAP;
    if (remaining && remaining / 2 < b) {
        b = (size_t)(remaining / 2);
    }
    *chunk = c;
    *budget = b;
}

void dlog_info_fill(dlog_info *info, uint16_t exit_reason, uint64_t exit_status,
                    uint32_t chunk_count, uint32_t chunk_size, uint64_t stored, uint64_t dropped,
                    uint64_t head, uint64_t run_time_us, const char *build_id) {
    memset(info, 0, sizeof *info);
    memcpy(info->magic, "DLOG", 4);
    info->version = DLOG_VERSION;
    info->exit_reason = exit_reason;
    info->exit_status = exit_status;
    info->chunk_count = chunk_count;
    info->chunk_size = chunk_size;
    info->stored_bytes = stored;
    info->dropped_bytes = dropped;
    info->head_bytes = head;
    info->run_time_us = run_time_us;
    strncpy(info->build_id, build_id, sizeof info->build_id - 1);
}
