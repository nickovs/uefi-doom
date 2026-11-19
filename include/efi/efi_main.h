/* Application lifecycle: entry, single exit path, cleanup registry.
 * See docs/architecture.md#lifecycle. */
#ifndef UD_EFI_MAIN_H
#define UD_EFI_MAIN_H

#include <stdint.h>

/** Why the application exited; recorded in the DoomLogInfo variable. */
typedef enum {
    EXIT_REASON_QUIT = 0,         /* normal user quit */
    EXIT_REASON_FATAL = 1,        /* I_Error / unrecoverable runtime error */
    EXIT_REASON_INIT_FAILURE = 2, /* a required component failed to start */
    EXIT_REASON_EMERGENCY = 3,    /* emergency quit chord */
} exit_reason;

/**
 * Provided by each application (HWPROBE, DOOM). Returning 0 is a normal quit;
 * any other value is treated as a fatal error.
 */
int app_main(int argc, char **argv);

/** Provided by each application: short name used as argv[0] and in logs. */
extern const char app_name[];

/** Leave the application from anywhere: runs every cleanup, then returns to firmware. */
_Noreturn void efi_exit(exit_reason reason);

typedef void (*cleanup_fn)(void *ctx);

/**
 * Register `fn(ctx)` to run at exit, in reverse order of registration. Call it
 * at the moment a resource is acquired. Registry overflow is fatal.
 */
void efi_cleanup_push(cleanup_fn fn, void *ctx, const char *name);

#endif
