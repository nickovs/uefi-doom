/* File access on the image's volume via EFI_SIMPLE_FILE_SYSTEM_PROTOCOL. */
#include "efi/fs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "efi/efi.h"

#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>

#include "efi/alloc.h"
#include "efi/efi_main.h"
#include "efi/log.h"
#include "efi/path_core.h"

EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;

int errno;

struct fs_file {
    EFI_FILE_PROTOCOL *h;
};

static EFI_FILE_PROTOCOL *root;
static uint16_t image_dir[PATH_MAX_UNITS + 1];
static bool read_only;

static int to_errno(EFI_STATUS st) {
    switch (st) {
    case EFI_NOT_FOUND:
        return -ENOENT;
    case EFI_WRITE_PROTECTED:
        return -EROFS;
    case EFI_ACCESS_DENIED:
        return -EACCES;
    case EFI_VOLUME_FULL:
        return -ENOSPC;
    case EFI_OUT_OF_RESOURCES:
        return -ENOMEM;
    case EFI_INVALID_PARAMETER:
        return -EINVAL;
    default:
        return -EIO;
    }
}

static void note_write_failure(EFI_STATUS st) {
    if ((st == EFI_WRITE_PROTECTED || st == EFI_ACCESS_DENIED) && !read_only) {
        read_only = true;
        log_msg(LOG_WARN, "fs: volume is read-only (%s); saving disabled", efi_status_str(st));
    }
}

static void root_cleanup(void *ctx) {
    (void)ctx;
    root->Close(root);
    root = NULL;
}

/* The image's file path is the last FILEPATH_DEVICE_PATH node(s) of
 * LoadedImage->FilePath; concatenate them. */
static bool image_path(EFI_DEVICE_PATH_PROTOCOL *dp, uint16_t *out, size_t cap) {
    size_t len = 0;
    for (; dp &&
           !(dp->Type == END_DEVICE_PATH_TYPE && dp->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE);) {
        size_t node_len = (size_t)dp->Length[0] | (size_t)dp->Length[1] << 8;
        if (node_len < 4) {
            return false; /* malformed */
        }
        if (dp->Type == MEDIA_DEVICE_PATH && dp->SubType == MEDIA_FILEPATH_DP) {
            const uint16_t *s = (const uint16_t *)((const uint8_t *)dp + 4);
            size_t units = (node_len - 4) / 2;
            for (size_t i = 0; i < units && s[i]; i++) {
                if (len + 1 >= cap) {
                    return false;
                }
                out[len++] = s[i];
            }
        }
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((uint8_t *)dp + node_len);
    }
    out[len] = 0;
    return len > 0;
}

bool fs_init(void) {
    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    EFI_STATUS st = gBS->HandleProtocol(gImage, &gEfiLoadedImageProtocolGuid, (void **)&li);
    if (EFI_ERROR(st) || !li) {
        log_msg(LOG_WARN, "fs: no LoadedImage (%s)", efi_status_str(st));
        return false;
    }
    st = gBS->HandleProtocol(li->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **)&sfs);
    if (EFI_ERROR(st) || !sfs) {
        log_msg(LOG_WARN, "fs: image volume has no file system (%s)", efi_status_str(st));
        return false;
    }
    st = sfs->OpenVolume(sfs, &root);
    if (EFI_ERROR(st)) {
        log_msg(LOG_WARN, "fs: OpenVolume failed (%s)", efi_status_str(st));
        return false;
    }
    efi_cleanup_push(root_cleanup, NULL, "fs root");

    uint16_t file[PATH_MAX_UNITS + 1];
    if (!image_path(li->FilePath, file, sizeof file / sizeof file[0]) ||
        path_dirname(file, image_dir, sizeof image_dir / sizeof image_dir[0]) < 0) {
        image_dir[0] = '\\';
        image_dir[1] = 0;
    }
    char dir8[PATH_MAX_UNITS * 3 + 1];
    size_t k = 0;
    for (size_t i = 0; image_dir[i] && k < sizeof dir8 - 1; i++) {
        dir8[k++] = image_dir[i] < 0x80 ? (char)image_dir[i] : '?';
    }
    dir8[k] = 0;
    log_msg(LOG_INFO, "fs: image directory %s", dir8);
    return true;
}

bool fs_read_only(void) {
    return read_only;
}

fs_file *fs_open(const char *path, fs_mode mode, int *err) {
    uint16_t p[PATH_MAX_UNITS + 1];
    *err = 0;
    if (!root) {
        *err = -ENOSYS;
        return NULL;
    }
    if (path_translate(path, image_dir, p) < 0) {
        *err = -ENAMETOOLONG;
        return NULL;
    }
    UINT64 omode = EFI_FILE_MODE_READ;
    if (mode != FS_READ) {
        omode |= EFI_FILE_MODE_WRITE;
    }
    if (mode == FS_WRITE) {
        omode |= EFI_FILE_MODE_CREATE;
    }
    fs_file *f = malloc(sizeof *f);
    if (!f) {
        *err = -ENOMEM;
        return NULL;
    }
    EFI_STATUS st = root->Open(root, &f->h, (CHAR16 *)p, omode, 0);
    if (EFI_ERROR(st)) {
        if (mode != FS_READ) {
            note_write_failure(st);
        }
        free(f);
        *err = to_errno(st);
        return NULL;
    }
    if (mode == FS_WRITE) {
        /* Truncate: EFI has no O_TRUNC; set the size to 0 via SetInfo. */
        UINTN sz = 0;
        f->h->GetInfo(f->h, &gEfiFileInfoGuid, &sz, NULL);
        EFI_FILE_INFO *info = sz ? malloc(sz) : NULL;
        if (info && !EFI_ERROR(f->h->GetInfo(f->h, &gEfiFileInfoGuid, &sz, info))) {
            if (info->Attribute & EFI_FILE_DIRECTORY) {
                free(info);
                f->h->Close(f->h);
                free(f);
                *err = -EISDIR;
                return NULL;
            }
            if (info->FileSize) {
                info->FileSize = 0;
                st = f->h->SetInfo(f->h, &gEfiFileInfoGuid, sz, info);
                if (EFI_ERROR(st)) {
                    note_write_failure(st);
                    free(info);
                    f->h->Close(f->h);
                    free(f);
                    *err = to_errno(st);
                    return NULL;
                }
            }
        }
        free(info);
    }
    return f;
}

long fs_read(fs_file *f, void *buf, size_t n) {
    UINTN sz = n;
    EFI_STATUS st = f->h->Read(f->h, &sz, buf);
    return EFI_ERROR(st) ? to_errno(st) : (long)sz;
}

long fs_write(fs_file *f, const void *buf, size_t n) {
    UINTN sz = n;
    EFI_STATUS st = f->h->Write(f->h, &sz, (void *)buf);
    if (EFI_ERROR(st)) {
        note_write_failure(st);
        return to_errno(st);
    }
    return sz == n ? (long)n : -ENOSPC;
}

int fs_seek(fs_file *f, uint64_t pos) {
    EFI_STATUS st = f->h->SetPosition(f->h, pos);
    return EFI_ERROR(st) ? to_errno(st) : 0;
}

uint64_t fs_tell(fs_file *f) {
    UINT64 pos = 0;
    f->h->GetPosition(f->h, &pos);
    return pos;
}

int64_t fs_size(fs_file *f) {
    UINTN sz = 0;
    f->h->GetInfo(f->h, &gEfiFileInfoGuid, &sz, NULL);
    EFI_FILE_INFO *info = sz ? malloc(sz) : NULL;
    if (!info) {
        return -ENOMEM;
    }
    EFI_STATUS st = f->h->GetInfo(f->h, &gEfiFileInfoGuid, &sz, info);
    int64_t r = EFI_ERROR(st) ? to_errno(st) : (int64_t)info->FileSize;
    free(info);
    return r;
}

int fs_flush(fs_file *f) {
    EFI_STATUS st = f->h->Flush(f->h);
    if (EFI_ERROR(st)) {
        note_write_failure(st);
        return to_errno(st);
    }
    return 0;
}

int fs_close(fs_file *f) {
    EFI_STATUS st = f->h->Close(f->h);
    free(f);
    return EFI_ERROR(st) ? to_errno(st) : 0;
}

int fs_delete(const char *path) {
    int err;
    fs_file *f = fs_open(path, FS_UPDATE, &err);
    if (!f) {
        return err;
    }
    /* Delete closes the handle whatever the outcome. */
    EFI_STATUS st = f->h->Delete(f->h);
    free(f);
    if (st == EFI_WARN_DELETE_FAILURE) {
        return -EACCES;
    }
    return EFI_ERROR(st) ? to_errno(st) : 0;
}

int fs_rename(const char *from, const char *to) {
    uint16_t a[PATH_MAX_UNITS + 1], b[PATH_MAX_UNITS + 1];
    uint16_t da[PATH_MAX_UNITS + 1], db[PATH_MAX_UNITS + 1];
    if (!root) {
        return -ENOSYS;
    }
    if (path_translate(from, image_dir, a) < 0 || path_translate(to, image_dir, b) < 0 ||
        path_dirname(a, da, PATH_MAX_UNITS + 1) < 0 ||
        path_dirname(b, db, PATH_MAX_UNITS + 1) < 0) {
        return -ENAMETOOLONG;
    }
    size_t i = 0;
    for (; da[i] && da[i] == db[i]; i++) {
    }
    if (da[i] != db[i]) {
        return -EINVAL; /* SetInfo renames within one directory only */
    }
    const uint16_t *leaf = b + i + (b[i] == '\\');
    size_t leaf_len = 0;
    while (leaf[leaf_len]) {
        leaf_len++;
    }
    int err;
    fs_file *f = fs_open(from, FS_UPDATE, &err);
    if (!f) {
        return err;
    }
    UINTN sz = 0;
    f->h->GetInfo(f->h, &gEfiFileInfoGuid, &sz, NULL);
    UINTN new_sz = SIZE_OF_EFI_FILE_INFO + (leaf_len + 1) * 2;
    EFI_FILE_INFO *info = malloc(sz > new_sz ? sz : new_sz);
    EFI_STATUS st = EFI_OUT_OF_RESOURCES;
    if (info && !EFI_ERROR(st = f->h->GetInfo(f->h, &gEfiFileInfoGuid, &sz, info))) {
        memcpy(info->FileName, leaf, (leaf_len + 1) * 2);
        info->Size = new_sz;
        st = f->h->SetInfo(f->h, &gEfiFileInfoGuid, new_sz, info);
        if (EFI_ERROR(st)) {
            note_write_failure(st);
        }
    }
    free(info);
    int c = fs_close(f);
    return EFI_ERROR(st) ? to_errno(st) : c;
}

int fs_mkdir(const char *path) {
    uint16_t p[PATH_MAX_UNITS + 1];
    EFI_FILE_PROTOCOL *h;
    if (!root) {
        return -ENOSYS;
    }
    if (path_translate(path, image_dir, p) < 0) {
        return -ENAMETOOLONG;
    }
    EFI_STATUS st = root->Open(root, &h, (CHAR16 *)p,
                               EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                               EFI_FILE_DIRECTORY);
    if (EFI_ERROR(st)) {
        note_write_failure(st);
        return to_errno(st);
    }
    h->Close(h);
    return 0;
}

int fs_read_whole(const char *path, void **buf, size_t *len) {
    int err;
    fs_file *f = fs_open(path, FS_READ, &err);
    if (!f) {
        return err;
    }
    int64_t size = fs_size(f);
    if (size < 0) {
        fs_close(f);
        return (int)size;
    }
    void *p = alloc_pages(size ? (size_t)size : 1);
    if (!p) {
        fs_close(f);
        return -ENOMEM;
    }
    size_t got = 0;
    while (got < (size_t)size) {
        long r = fs_read(f, (uint8_t *)p + got, (size_t)size - got);
        if (r <= 0) {
            fs_close(f);
            return r < 0 ? (int)r : -EIO; /* short file: size changed under us */
        }
        got += (size_t)r;
    }
    fs_close(f);
    *buf = p;
    *len = got;
    return 0;
}

int fs_write_whole(const char *path, const void *buf, size_t len) {
    int err;
    fs_file *f = fs_open(path, FS_WRITE, &err);
    if (!f) {
        return err;
    }
    long w = fs_write(f, buf, len);
    int c = fs_close(f);
    return w < 0 ? (int)w : c;
}
