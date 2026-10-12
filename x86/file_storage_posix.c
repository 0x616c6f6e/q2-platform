#define _POSIX_C_SOURCE 200809L
#include "file_storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#define MEDIA_SIZE (256u * 1024u)
struct q2_pc_storage {
    int file;
    q2_storage_t *storage;
};
/* Positional I/O avoids shared seek state; short operations and signals cannot truncate a block. */
static q2_result_t transfer(q2_pc_storage_t *s, uint64_t off, void *data, size_t n, bool writing) {
    if (off > MEDIA_SIZE || n > MEDIA_SIZE - off || (!data && n))
        return Q2_ERR_ARGUMENT;
    size_t done = 0;
    while (done < n) {
        ssize_t count = writing ? pwrite(s->file, (const char *)data + done, n - done, (off_t)(off + done))
                                : pread(s->file, (char *)data + done, n - done, (off_t)(off + done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return Q2_ERR_IO;
        done += (size_t)count;
    }
    return Q2_OK;
}
static q2_result_t read_media(void *ctx, uint64_t off, void *data, size_t n) {
    return transfer(ctx, off, data, n, false);
}
static q2_result_t write_media(void *ctx, uint64_t off, const void *data, size_t n) {
    return transfer(ctx, off, (void *)data, n, true);
}
static q2_result_t erase_media(void *ctx, uint64_t off, size_t n) {
    unsigned char erased[4096];
    memset(erased, 255, sizeof(erased));
    if (n != sizeof(erased))
        return Q2_ERR_ARGUMENT;
    return write_media(ctx, off, erased, n);
}
static q2_result_t sync_media(void *ctx) {
    return fsync(((q2_pc_storage_t *)ctx)->file) == 0 ? Q2_OK : Q2_ERR_IO;
}
q2_result_t q2_pc_storage_open(q2_kernel_t *kernel, const char *path, q2_pc_storage_t **out) {
    if (!kernel || !path || !*path || !out)
        return Q2_ERR_ARGUMENT;
    *out = NULL;
    q2_pc_storage_t *s = calloc(1, sizeof(*s));
    if (!s)
        return Q2_ERR_MEMORY;
    s->file = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool fresh = s->file >= 0;
    if (!fresh && errno == EEXIST)
        s->file = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (s->file < 0) {
        free(s);
        return Q2_ERR_IO;
    }
    /* Never share writable media between simulator processes. */
    if (flock(s->file, LOCK_EX | LOCK_NB) != 0) {
        close(s->file);
        free(s);
        return Q2_ERR_IO;
    }
    struct stat stat;
    q2_result_t e = Q2_OK;
    if (fstat(s->file, &stat) != 0 || !S_ISREG(stat.st_mode))
        e = Q2_ERR_IO;
    else if (!fresh && stat.st_size != MEDIA_SIZE)
        e = Q2_ERR_FORMAT;
    if (fresh && e == Q2_OK)
        for (unsigned off = 0; off < MEDIA_SIZE && e == Q2_OK; off += 4096)
            e = erase_media(s, off, 4096);
    q2_block_device_t device = {1,  sizeof(device), s,           4096,        64,        16,
                                16, read_media,     write_media, erase_media, sync_media};
    if (e == Q2_OK)
        e = q2_littlefs_create(kernel, &device, &s->storage);
    if (e == Q2_OK && fresh)
        e = q2_storage_format(s->storage);
    const q2_component_t *component;
    if (e == Q2_OK)
        e = q2_storage_component(s->storage, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
    if (e != Q2_OK) {
        q2_pc_storage_close(&s);
        return e;
    }
    *out = s;
    return Q2_OK;
}
q2_result_t q2_pc_storage_close(q2_pc_storage_t **ref) {
    if (!ref)
        return Q2_ERR_ARGUMENT;
    if (!*ref)
        return Q2_OK;
    q2_pc_storage_t *s = *ref;
    if (s->storage) {
        q2_result_t e = q2_storage_destroy(&s->storage);
        if (e != Q2_OK)
            return e;
    }
    bool ok = close(s->file) == 0;
    free(s);
    *ref = NULL;
    return ok ? Q2_OK : Q2_ERR_IO;
}
