/* HWPROBE internals: probe sections and the JSON report builder.
 * See docs/architecture.md#hwprobe. */
#ifndef UD_HWPROBE_H
#define UD_HWPROBE_H

#include <stdbool.h>
#include <stdint.h>

/** True unless -interactive: no prompts, no audible tones, no risky experiments. */
extern bool hw_batch;

/** True with -wc: run the write-combining experiment without asking. */
extern bool hw_wc;

/** True with -avx: enable YMM state for the session (restored) and time AVX stores. */
extern bool hw_avx;

/** True with -patwc: map the framebuffer WC via PAT + page tables (restored). */
extern bool hw_patwc;

/* --- JSON report (written to HWPROBE.JSON at the end) ---------------------- */
void rep_obj(const char *key); /* key NULL inside arrays / at top level */
void rep_arr(const char *key);
void rep_end(void);
void rep_str(const char *key, const char *v);
void rep_int(const char *key, int64_t v);
void rep_hex(const char *key, uint64_t v); /* as "0x..." string */
void rep_bool(const char *key, bool v);
void rep_num(const char *key, double v);
/** Write the finished report; returns 0 or a negative errno. */
int rep_save(const char *path);

/** Ask a yes/no question on the console; false on timeout or in batch mode. */
bool hw_ask(const char *question, uint32_t timeout_ms);

/** Announce the next interactive step and wait for ENTER; false on timeout or in batch. */
bool hw_wait_enter(const char *next, uint32_t timeout_ms);

/** Median of n values (sorts in place). */
uint64_t hw_median(uint64_t *v, int n);

/* --- Sections ------------------------------------------------------------------ */
void probe_system(void);
void probe_clock(void);
void probe_acpi(void);
void probe_video(void);
void probe_input(void);
void probe_speaker(void);

#endif
