/* FILE shim (src/libc/file.c) over the in-memory fake fs. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "fake_fs.h"
#include "libc_pure.h"

int main(void) {
    struct ud_file *f;
    size_t len;
    char buf[64];

    CHECK(!shim_fopen("missing", "rb"));
    CHECK_EQ(errno, ENOENT);
    CHECK(!shim_fopen("x", "a"));
    CHECK_EQ(errno, EINVAL);

    /* Small writes are buffered and land on close. */
    f = shim_fopen("cfg", "w");
    CHECK(f);
    CHECK_EQ(shim_fprintf(f, "key %d\n", 42), 7);
    CHECK_EQ(shim_fputs("x", f), 0);
    CHECK_EQ(shim_fputc('y', f), 'y');
    CHECK_EQ(shim_ftell(f), 9);
    CHECK_EQ(fake_fs_writes, 0);
    CHECK_EQ(shim_fclose(f), 0);
    const char *data = fake_fs_get("cfg", &len);
    CHECK_EQ(len, 9);
    CHECK(memcmp(data, "key 42\nxy", 9) == 0);

    /* "w" truncates. */
    f = shim_fopen("cfg", "wb");
    shim_fclose(f);
    fake_fs_get("cfg", &len);
    CHECK_EQ(len, 0);

    /* Byte-at-a-time reads (savegame loader) hit fs once per 4 KiB. */
    static unsigned char big[10000];
    for (size_t i = 0; i < sizeof big; i++) {
        big[i] = (unsigned char)(i * 7);
    }
    fake_fs_put("save", big, sizeof big);
    f = shim_fopen("save", "rb");
    fake_fs_reads = 0;
    int ok = 1;
    for (size_t i = 0; i < sizeof big; i++) {
        ok &= shim_fgetc(f) == big[i];
    }
    CHECK(ok);
    CHECK(fake_fs_reads <= 3);
    CHECK(!shim_feof(f));
    CHECK_EQ(shim_fgetc(f), EOF);
    CHECK(shim_feof(f));

    /* Seek and tell account for read-ahead; SEEK_END uses the size. */
    CHECK_EQ(shim_fseek(f, 100, SEEK_SET), 0);
    CHECK(!shim_feof(f));
    CHECK_EQ(shim_fgetc(f), big[100]);
    CHECK_EQ(shim_ftell(f), 101);
    CHECK_EQ(shim_fseek(f, -1, SEEK_CUR), 0);
    CHECK_EQ(shim_fgetc(f), big[100]);
    CHECK_EQ(shim_fseek(f, 0, SEEK_END), 0);
    CHECK_EQ(shim_ftell(f), (long)sizeof big);
    CHECK_EQ(shim_fseek(f, -20000, SEEK_CUR), -1);
    CHECK_EQ(shim_fseek(f, 0, 99), -1);

    /* A large read goes straight to fs, and a short read stops at EOF. */
    static unsigned char out[sizeof big];
    shim_fseek(f, 0, SEEK_SET);
    CHECK_EQ(shim_fread(out, 1, sizeof out, f), sizeof out);
    CHECK(memcmp(out, big, sizeof big) == 0);
    shim_fseek(f, 9990, SEEK_SET);
    CHECK_EQ(shim_fread(out, 4, 5, f), 2);  /* 10 bytes left = 2 whole items */
    CHECK_EQ(shim_fwrite("z", 1, 1, f), 0); /* read-only */
    CHECK(shim_ferror(f));
    shim_fclose(f);

    /* r+: read, then overwrite in place, then read on. */
    fake_fs_put("rw", "abcdef", 6);
    f = shim_fopen("rw", "r+b");
    CHECK_EQ(shim_fread(buf, 1, 2, f), 2);
    CHECK_EQ(shim_fwrite("XY", 1, 2, f), 2);
    CHECK_EQ(shim_fgetc(f), 'e');
    shim_fclose(f);
    data = fake_fs_get("rw", &len);
    CHECK(len == 6 && memcmp(data, "abXYef", 6) == 0);

    /* Long formatted output is not truncated. */
    f = shim_fopen("long", "w");
    char line[1000];
    memset(line, 'q', sizeof line - 1);
    line[sizeof line - 1] = 0;
    CHECK_EQ(shim_fprintf(f, "<%s>", line), 1001);
    shim_fclose(f);
    fake_fs_get("long", &len);
    CHECK_EQ(len, 1001);

    /* Savegame commit: remove the old file, rename the temp file over it. */
    fake_fs_put("doomsav0.dsg", "old", 3);
    fake_fs_put("temp.dsg", "new", 3);
    CHECK_EQ(shim_rename("temp.dsg", "doomsav0.dsg"), -1); /* target exists */
    CHECK_EQ(shim_remove("doomsav0.dsg"), 0);
    CHECK_EQ(shim_rename("temp.dsg", "doomsav0.dsg"), 0);
    data = fake_fs_get("doomsav0.dsg", &len);
    CHECK(data && memcmp(data, "new", 3) == 0);
    CHECK_EQ(shim_remove("nope"), -1);
    CHECK_EQ(errno, ENOENT);

    /* Config loading: m_config.c's loop over a file without a final newline. */
    const char cfg[] = "mouse_sensitivity 5\nkey_up 119\n\nchatmacro0 \"hi there\"";
    fake_fs_put("default.cfg", cfg, sizeof cfg - 1);
    f = shim_fopen("default.cfg", "r");
    char name[80], val[100];
    int lines = 0, good = 0;
    while (!shim_feof(f) && lines < 10) {
        lines++;
        if (shim_fscanf(f, "%79s %99[^\n]\n", name, val) == 2) {
            good++;
        }
    }
    CHECK_EQ(good, 3);
    CHECK_STR(name, "chatmacro0");
    CHECK_STR(val, "\"hi there\"");
    CHECK(lines <= 5);
    CHECK(!shim_fgets(buf, sizeof buf, f));
    shim_fclose(f);

    /* fgets stops after a newline and at n-1 bytes. */
    fake_fs_put("lines", "ab\ncdef", 7);
    f = shim_fopen("lines", "r");
    CHECK_STR(shim_fgets(buf, sizeof buf, f), "ab\n");
    CHECK_STR(shim_fgets(buf, 3, f), "cd");
    CHECK_STR(shim_fgets(buf, sizeof buf, f), "ef");
    CHECK(!shim_fgets(buf, sizeof buf, f));
    shim_fclose(f);

    /* stdout/stderr go to the log; they cannot seek. */
    CHECK_EQ(shim_fprintf(shim_stderr, "warn %s\n", "x"), 7);
    CHECK_EQ(shim_fputs("out", shim_stdout), 0);
    CHECK_STR(fake_log, "warn x\nout");
    CHECK_EQ(shim_ftell(shim_stdout), -1);
    CHECK_EQ(shim_fileno(shim_stderr), 2);
    CHECK_EQ(shim_fflush(shim_stdout), 0);
    return CHECK_DONE();
}
