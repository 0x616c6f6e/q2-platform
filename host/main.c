/* Host reference adapter: a private RAM disk and console text provider, not a hardware simulation. */
#include "log_console.h"
#include "q2/display.h"
#include "q2/kernel.h"
#include "q2/runtime.h"
#include "q2/sqlite.h"
#include "q2/storage.h"
#include "q2_platform_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
static void *allocate(void *ctx, size_t n) {
    (void)ctx;
    return malloc(n);
}
static void release(void *ctx, void *p) {
    (void)ctx;
    free(p);
}
static uint64_t now(void *ctx) {
    (void)ctx;
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
#endif
}
#if Q2_COMPONENT_STORAGE
typedef struct {
    unsigned char *bytes;
    size_t size;
} media_t;
static q2_result_t read_media(void *ctx, uint64_t off, void *out, size_t n) {
    media_t *m = ctx;
    if (off > m->size || n > m->size - off)
        return Q2_ERR_IO;
    memcpy(out, m->bytes + (size_t)off, n);
    return Q2_OK;
}
static q2_result_t write_media(void *ctx, uint64_t off, const void *data, size_t n) {
    media_t *m = ctx;
    if (off > m->size || n > m->size - off)
        return Q2_ERR_IO;
    for (size_t i = 0; i < n; i++)
        if ((m->bytes[off + i] & ((const unsigned char *)data)[i]) != ((const unsigned char *)data)[i])
            return Q2_ERR_IO;
    memcpy(m->bytes + (size_t)off, data, n);
    return Q2_OK;
}
static q2_result_t erase_media(void *ctx, uint64_t off, size_t n) {
    media_t *m = ctx;
    if (off > m->size || n > m->size - off)
        return Q2_ERR_IO;
    memset(m->bytes + (size_t)off, 255, n);
    return Q2_OK;
}
static q2_result_t sync_media(void *ctx) {
    (void)ctx;
    return Q2_OK;
}
#endif
static q2_result_t clear(void *ctx) {
    (void)ctx;
    puts("[display cleared]");
    return Q2_OK;
}
static q2_result_t text(void *ctx, unsigned x, unsigned y, const char *value) {
    (void)ctx;
    printf("[%u,%u] %s\n", x, y, value);
    return Q2_OK;
}
static char *read_file(const char *path, size_t limit, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *data = malloc(limit + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(data, 1, limit + 1, f);
    bool ok = !ferror(f) && n <= limit;
    fclose(f);
    if (!ok) {
        free(data);
        return NULL;
    }
    data[n] = 0;
    *out_size = n;
    return data;
}
static int report(q2_result_t e) {
    const char *name;
    q2_result_name(e, &name);
    fprintf(stderr, "Q2 result: %s (%d)\n", name, e);
    return e == Q2_OK ? 0 : 1;
}
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: q2_host manifest.json bundled-main.lua [explicit-grant ...]\n");
        return 2;
    }
    size_t json_size = 0, code_size = 0;
    char *json = read_file(argv[1], 4096, &json_size), *code = read_file(argv[2], 32768, &code_size);
    if (!json || !code) {
        free(json);
        free(code);
        return report(Q2_ERR_IO);
    }
    q2_kernel_t *kernel = NULL;
    q2_lua_runtime_t *runtime = NULL;
#if Q2_COMPONENT_STORAGE
    q2_storage_t *storage = NULL;
#endif
#if Q2_COMPONENT_SQLITE
    q2_sqlite_t *database = NULL;
#endif
    q2_display_t *display = NULL;
    q2_result_t e = Q2_OK;
#if Q2_COMPONENT_STORAGE
    media_t media = {malloc(256 * 1024), 256 * 1024};
    e = media.bytes ? Q2_OK : Q2_ERR_MEMORY;
#endif
    q2_port_t port = {1, sizeof(port), NULL, allocate, release, now};
    if (e == Q2_OK)
        e = q2_kernel_create(&port, &kernel);
    if (e == Q2_OK) {
        const char *setting = getenv("Q2_LOG_LEVEL");
        q2_log_level_t level = (q2_log_level_t)Q2_DEFAULT_LOG_LEVEL;
        if (setting)
            e = q2_console_log_parse(setting, &level);
        if (e == Q2_OK)
            e = q2_console_log_attach(kernel, level);
    }
    const q2_component_t *component;
#if Q2_COMPONENT_STORAGE
    if (e == Q2_OK) {
        memset(media.bytes, 255, media.size);
        q2_block_device_t block = {1,  sizeof(block), &media,      4096,        64,        16,
                                   16, read_media,    write_media, erase_media, sync_media};
        e = q2_littlefs_create(kernel, &block, &storage);
    }
    /* The host demo explicitly formats only newly allocated RAM, never a real disk. */
    if (e == Q2_OK)
        e = q2_storage_format(storage);
    if (e == Q2_OK)
        e = q2_storage_component(storage, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
#endif
    if (Q2_COMPONENT_DISPLAY) {
        q2_display_backend_t backend = {
            1, sizeof(backend), NULL, Q2_DISPLAY_WIDTH, Q2_DISPLAY_HEIGHT, clear, text};
        if (e == Q2_OK)
            e = q2_display_create(&backend, &display);
        if (e == Q2_OK)
            e = q2_display_component(display, &component);
        if (e == Q2_OK)
            e = q2_component_register(kernel, component);
    }
#if Q2_COMPONENT_SQLITE
    if (e == Q2_OK)
        e = q2_sqlite_create(&database);
    if (e == Q2_OK)
        e = q2_sqlite_component(database, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
#endif
    if (e == Q2_OK)
        e = q2_kernel_boot(kernel);
    if (e == Q2_OK)
        e = q2_lua_create(kernel, &runtime);
    if (e == Q2_OK)
        e = q2_lua_start(runtime, json, json_size, code, code_size, (const char *const *)(argv + 3),
                         (size_t)(argc - 3));
    if (e == Q2_OK)
        e = q2_kernel_poll(kernel, 8);
    if (e != Q2_OK && runtime) {
        q2_app_status_t s;
        q2_lua_status(runtime, &s);
        fprintf(stderr, "%s\n", s.error);
    }
    q2_result_t cleanup = Q2_OK;
    if (runtime)
        cleanup = q2_lua_destroy(&runtime);
    if (cleanup == Q2_OK && kernel)
        cleanup = q2_kernel_destroy(&kernel);
#if Q2_COMPONENT_STORAGE
    if (cleanup == Q2_OK && storage)
        cleanup = q2_storage_destroy(&storage);
#endif
#if Q2_COMPONENT_SQLITE
    if (cleanup == Q2_OK && database)
        cleanup = q2_sqlite_destroy(&database);
#endif
    if (cleanup == Q2_OK && display)
        cleanup = q2_display_destroy(&display);
    free(json);
    free(code);
#if Q2_COMPONENT_STORAGE
    free(media.bytes);
#endif
    return report(e == Q2_OK ? cleanup : e);
}
