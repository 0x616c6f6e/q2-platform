#define WIN32_LEAN_AND_MEAN
#include "file_storage.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#define MEDIA_SIZE (256u * 1024u)
struct q2_pc_storage {
    HANDLE file;
    q2_storage_t *storage;
};
static q2_result_t seek(q2_pc_storage_t *s, uint64_t off, size_t n) {
    if (off > MEDIA_SIZE || n > MEDIA_SIZE - off)
        return Q2_ERR_ARGUMENT;
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)off;
    return SetFilePointerEx(s->file, position, NULL, FILE_BEGIN) ? Q2_OK : Q2_ERR_IO;
}
static q2_result_t read_media(void *ctx, uint64_t off, void *data, size_t n) {
    q2_pc_storage_t *s = ctx;
    DWORD count;
    if (seek(s, off, n) != Q2_OK)
        return Q2_ERR_IO;
    return ReadFile(s->file, data, (DWORD)n, &count, NULL) && count == n ? Q2_OK : Q2_ERR_IO;
}
static q2_result_t write_media(void *ctx, uint64_t off, const void *data, size_t n) {
    q2_pc_storage_t *s = ctx;
    DWORD count;
    if (seek(s, off, n) != Q2_OK)
        return Q2_ERR_IO;
    return WriteFile(s->file, data, (DWORD)n, &count, NULL) && count == n ? Q2_OK : Q2_ERR_IO;
}
static q2_result_t erase_media(void *ctx, uint64_t off, size_t n) {
    unsigned char erased[4096];
    memset(erased, 255, sizeof(erased));
    if (n != sizeof(erased))
        return Q2_ERR_ARGUMENT;
    return write_media(ctx, off, erased, n);
}
static q2_result_t sync_media(void *ctx) {
    return FlushFileBuffers(((q2_pc_storage_t *)ctx)->file) ? Q2_OK : Q2_ERR_IO;
}
q2_result_t q2_pc_storage_open(q2_kernel_t *kernel, const char *path, q2_pc_storage_t **out) {
    if (!kernel || !path || !*path || !out)
        return Q2_ERR_ARGUMENT;
    *out = NULL;
    q2_pc_storage_t *s = calloc(1, sizeof(*s));
    if (!s)
        return Q2_ERR_MEMORY;
    s->file =
        CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
    if (s->file == INVALID_HANDLE_VALUE) {
        free(s);
        return Q2_ERR_IO;
    }
    LARGE_INTEGER size;
    q2_result_t e = Q2_OK;
    if (!GetFileSizeEx(s->file, &size))
        e = Q2_ERR_IO;
    else if (!fresh && size.QuadPart != MEDIA_SIZE)
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
    bool ok = CloseHandle(s->file);
    free(s);
    *ref = NULL;
    return ok ? Q2_OK : Q2_ERR_IO;
}
