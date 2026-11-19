/* Test hooks of tests/unit/fake_fs.c. */
#ifndef UD_TEST_FAKE_FS_H
#define UD_TEST_FAKE_FS_H

#include <stddef.h>

extern char fake_log[4096];
extern size_t fake_log_len;
extern int fake_fs_reads, fake_fs_writes;

void fake_fs_put(const char *path, const void *data, size_t len);
const void *fake_fs_get(const char *path, size_t *len);

#endif
