/* libc pieces that touch the platform or the FPU: no environment, no shell,
 * sleeping on the calibrated clock, assertions, and the few maths functions the
 * engine links (no libm). */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "efi/clock.h"
#include "efi/efi_main.h"
#include "efi/fs.h"
#include "efi/log.h"

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

int system(const char *cmd) {
    (void)cmd;
    return -1;
}

int isatty(int fd) {
    (void)fd;
    return 0;
}

int unlink(const char *path) {
    return remove(path);
}

int usleep(unsigned int us) {
    clock_sleep_us(us);
    return 0;
}

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    int r = fs_mkdir(path);
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return 0;
}

_Noreturn void __assert_fail(const char *expr, const char *file, int line) {
    log_msg(LOG_ERROR, "assertion failed: %s (%s:%d)", expr, file, line);
    efi_exit(EXIT_REASON_FATAL);
}

double fabs(double x) {
    return __builtin_fabs(x);
}
