#include "audio_win32.h"
#include "device_data.h"
#include "network.h"
/* Native PC runner: platform ownership stays here; Lua and LVGL stay inside Q2-Kernel. */
#define WIN32_LEAN_AND_MEAN
#include "assets.h"
#include "catalog.h"
#include "file_storage.h"
#include "log_console.h"
#include "q2/runtime.h"
#include "q2/sqlite.h"
#include "q2_platform_config.h"
#if defined(__linux__) && Q2_COMPONENT_AUDIO
#error "Linux SDL2: disable components.audio or select q2.linux.json"
#endif
#include "pc_time.h"
#include "web.h"
#include "window.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return q2_pc_now_ms();
}
static char *read_file(const char *path, size_t limit, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *bytes = malloc(limit + 1);
    if (!bytes) {
        fclose(f);
        return NULL;
    }
    *size = fread(bytes, 1, limit + 1, f);
    bool ok = !ferror(f) && *size <= limit;
    fclose(f);
    if (!ok) {
        free(bytes);
        return NULL;
    }
    bytes[*size] = 0;
    return bytes;
}
static bool number(const char *text, unsigned max, unsigned *out) {
    char *end;
    if (!*text || *text == '-')
        return false;
    unsigned long n = strtoul(text, &end, 10);
    if (*end || n > max)
        return false;
    *out = (unsigned)n;
    return true;
}
static int usage(void) {
    fputs(
        "Usage: q2_x86 manifest.json bundled-main.lua --grant CAP [--grant CAP ...]\n"
        "  [--width 502 --height 410] [--headless --frames 30] [--capture frame.bmp]\n"
        "  [--log-level off|error|warn|info|debug] (default: info)\n"
        "  [--storage-file private.lfs] (persistent APP preferences)\n"
        "  [--home UUID --initial-factory --factory-app manifest source --app manifest source]\n"
        "  [--web-port 8765] [--key NAME ...] (loopback hardware simulator)\n"
        "  [--click X Y] [--scroll-down N | --scroll-up N] (automated pointer test after the first frame)\n",
        stderr);
    return 2;
}
int main(int argc, char **argv) {
    if (argc < 3)
        return usage();
    bool headless = false, web_enabled = false;
    unsigned web_port = 8765, key_count = 0;
    const char *key_names[Q2_KEYS_MAX];
    unsigned width = Q2_DISPLAY_WIDTH, height = Q2_DISPLAY_HEIGHT, frames = 0;
    unsigned clicks[32][2], click_count = 0;
    const char *capture = NULL, *grants[Q2_CAPABILITY_MAX];
    const char *storage_file = NULL;
    const char *home_id = NULL, *extra_manifest[Q2_APPS_MAX], *extra_source[Q2_APPS_MAX];
    bool initial_factory = false, extra_factory[Q2_APPS_MAX];
    unsigned extra_count = 0;
    size_t grant_count = 0;
    q2_log_level_t log_level = (q2_log_level_t)Q2_DEFAULT_LOG_LEVEL;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--headless"))
            headless = true;
        else if (!strcmp(argv[i], "--web-port") && i + 1 < argc) {
            if (!number(argv[++i], 65535, &web_port))
                return usage();
            web_enabled = true;
        } else if (!strcmp(argv[i], "--key") && i + 1 < argc) {
            if (key_count == Q2_KEYS_MAX)
                return usage();
            key_names[key_count++] = argv[++i];
        } else if (!strcmp(argv[i], "--home") && i + 1 < argc)
            home_id = argv[++i];
        else if (!strcmp(argv[i], "--initial-factory"))
            initial_factory = true;
        else if ((!strcmp(argv[i], "--factory-app") || !strcmp(argv[i], "--app")) && i + 2 < argc) {
            if (extra_count == Q2_APPS_MAX - 1)
                return usage();
            extra_factory[extra_count] = !strcmp(argv[i], "--factory-app");
            extra_manifest[extra_count] = argv[++i];
            extra_source[extra_count++] = argv[++i];
        } else if (!strcmp(argv[i], "--storage-file") && i + 1 < argc)
            storage_file = argv[++i];
        else if (!strcmp(argv[i], "--log-level") && i + 1 < argc) {
            if (q2_console_log_parse(argv[++i], &log_level) != Q2_OK)
                return usage();
        } else if (!strcmp(argv[i], "--grant") && i + 1 < argc && grant_count < Q2_CAPABILITY_MAX)
            grants[grant_count++] = argv[++i];
        else if (!strcmp(argv[i], "--capture") && i + 1 < argc)
            capture = argv[++i];
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) {
            if (!number(argv[++i], 1024, &width) || width < 64)
                return usage();
        } else if (!strcmp(argv[i], "--height") && i + 1 < argc) {
            if (!number(argv[++i], 1024, &height) || height < 64)
                return usage();
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            if (!number(argv[++i], 1000000, &frames) || !frames)
                return usage();
        } else if ((!strcmp(argv[i], "--scroll-down") || !strcmp(argv[i], "--scroll-up")) && i + 1 < argc) {
            if (click_count == 32)
                return usage();
            clicks[click_count][0] = !strcmp(argv[i], "--scroll-down") ? UINT_MAX : UINT_MAX - 1;
            if (!number(argv[++i], 8192, &clicks[click_count][1]))
                return usage();
            click_count++;
        } else if (!strcmp(argv[i], "--click") && i + 2 < argc) {
            if (click_count == 32 || !number(argv[++i], 1023, &clicks[click_count][0]) ||
                !number(argv[++i], 1023, &clicks[click_count][1]))
                return usage();
            click_count++;
        } else
            return usage();
    }
    /* Product-disabled providers cannot be re-enabled through CLI grants or options. */
    if ((!Q2_WEB_ENABLED && web_enabled) || (!Q2_COMPONENT_KEYS && key_count) ||
        (!Q2_COMPONENT_STORAGE && storage_file) || (!Q2_COMPONENT_APPS && home_id)) {
        fputs("Requested feature is disabled by product configuration\n", stderr);
        return 2;
    }
    if (headless && !frames)
        return usage();
    if (!home_id && (initial_factory || extra_count))
        return usage();
    for (unsigned i = 0; i < click_count; i++)
        if (clicks[i][0] < UINT_MAX - 1 && (clicks[i][0] >= width || clicks[i][1] >= height))
            return usage();
    size_t json_size = 0, source_size = 0;
    char *json = read_file(argv[1], Q2_MANIFEST_MAX, &json_size);
    char *source = read_file(argv[2], Q2_SOURCE_MAX, &source_size);
    q2_result_t e = json && source ? Q2_OK : Q2_ERR_IO;
    q2_kernel_t *kernel = NULL;
    q2_lua_runtime_t *runtime = NULL;
    q2_lvgl_t *display = NULL;
    q2_pc_window_t *window = NULL;
#if Q2_COMPONENT_STORAGE
    q2_pc_storage_t *storage = NULL;
#endif
#if Q2_COMPONENT_SQLITE
    q2_sqlite_t *database = NULL;
#endif
    q2_apps_t *apps = NULL;
    q2_keys_t *keys = NULL;
    q2_data_t *data = NULL;
    q2_pc_device_data_t device_data = {0};
    q2_pc_web_t *web = NULL;
#if Q2_COMPONENT_AUDIO
    q2_audio_t *audio = NULL;
    q2_pc_audio_t *audio_backend = NULL;
    q2_audio_backend_t audio_port;
#endif
#if Q2_COMPONENT_NETWORK
    q2_network_t *network = NULL;
    q2_pc_network_t *transport = NULL;
    q2_network_backend_t network_backend;
#endif
    q2_pc_catalog_t catalog = {.home_id = home_id};
    q2_manifest_t manifest;
    if (e == Q2_OK)
        e = q2_manifest_parse(json, json_size, &manifest);
    q2_port_t port = {Q2_PORT_ABI, sizeof(port), NULL, allocate, release, now};
    const q2_component_t *component;
    q2_pixel_backend_t backend;
    if (e == Q2_OK)
        e = q2_kernel_create(&port, &kernel);
    if (e == Q2_OK)
        e = q2_console_log_attach(kernel, log_level);
    Q2_LOGI(kernel, "x86", "starting %ux%u headless=%d", width, height, headless);
    if (e == Q2_OK)
        e = q2_pc_window_create(width, height, headless, &window);
    if (e == Q2_OK)
        e = q2_pc_window_backend(window, &backend);
    if (e == Q2_OK)
        e = q2_lvgl_create(kernel, &backend, &display);
    const q2_system_ui_config_t system_ui = {Q2_STATUS_BAR_ENABLED, Q2_SCREENSAVER_ENABLED,
                                             Q2_STATUS_BAR_HEIGHT, Q2_IDLE_TIMEOUT_MS,
                                             Q2_CLOCK_MOVE_INTERVAL_MS};
    if (e == Q2_OK)
        e = q2_lvgl_system_configure(display, &system_ui);
    const q2_lifecycle_config_t lifecycle = {.lock_enabled = Q2_LOCK_ENABLED,
                                             .status_left = Q2_STATUS_LEFT,
                                             .status_right = Q2_STATUS_RIGHT,
                                             .saver_text = Q2_SAVER_TEXT,
                                             .unlock_key = Q2_UNLOCK_KEY,
                                             .unlock_action = Q2_UNLOCK_ACTION};
    if (e == Q2_OK && home_id)
        e = q2_lvgl_lifecycle(display, home_id, &lifecycle);
    if (e == Q2_OK && !home_id)
        e = q2_pc_assets_load(display, argv[1], manifest.id);
    if (e == Q2_OK)
        e = q2_lvgl_component(display, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
#if Q2_COMPONENT_STORAGE
    if (e == Q2_OK && storage_file)
        e = q2_pc_storage_open(kernel, storage_file, &storage);
#endif
#if Q2_COMPONENT_SQLITE
    if (e == Q2_OK)
        e = q2_sqlite_create(&database);
    if (e == Q2_OK)
        e = q2_sqlite_component(database, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
#endif
    if (Q2_COMPONENT_APPS && e == Q2_OK && home_id) {
        catalog.display = display;
        e = q2_apps_create(kernel, q2_pc_catalog_prepare, &catalog, &apps);
        if (e == Q2_OK)
            e = q2_pc_catalog_add(&catalog, apps, argv[1], argv[2], grants, grant_count, initial_factory);
        for (unsigned i = 0; i < extra_count && e == Q2_OK; i++)
            e = q2_pc_catalog_add(&catalog, apps, extra_manifest[i], extra_source[i], grants, grant_count,
                                  extra_factory[i]);
        if (e == Q2_OK)
            e = q2_apps_component(apps, &component);
        if (e == Q2_OK)
            e = q2_component_register(kernel, component);
    }
#if Q2_COMPONENT_NETWORK
    if (Q2_COMPONENT_NETWORK && e == Q2_OK)
        e = q2_pc_network_create(&transport, &network_backend);
    if (transport && e == Q2_OK)
        e = q2_network_create(kernel, &network_backend, &network);
    if (network && e == Q2_OK)
        e = q2_network_component(network, &component);
    if (network && e == Q2_OK)
        e = q2_component_register(kernel, component);
#endif
#if Q2_COMPONENT_AUDIO
    if (Q2_COMPONENT_AUDIO && e == Q2_OK)
        e = q2_pc_audio_create(&audio_backend, &audio_port);
    if (audio_backend && e == Q2_OK)
        e = q2_audio_create(&audio_port, &audio);
    if (audio && e == Q2_OK)
        e = q2_audio_component(audio, &component);
    if (audio && e == Q2_OK)
        e = q2_component_register(kernel, component);
#endif
    if (Q2_COMPONENT_DATA && e == Q2_OK)
        e = q2_data_create(kernel, &data);
    if (data && e == Q2_OK)
        e = q2_pc_device_data_register(&device_data, data);
    if (Q2_COMPONENT_KEYS) {
        if (e == Q2_OK)
            e = q2_keys_create(kernel, &keys);
        if (!key_count) {
            const char *defaults[] = Q2_DEFAULT_KEY_NAMES;
            for (unsigned i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
                key_names[key_count++] = defaults[i];
        }
        for (unsigned i = 0; i < key_count && e == Q2_OK; i++)
            e = q2_keys_register(keys, key_names[i]);
        if (e == Q2_OK)
            e = q2_keys_component(keys, &component);
        if (e == Q2_OK)
            e = q2_component_register(kernel, component);
    }
    if (data && keys && e == Q2_OK)
        e = q2_keys_data(keys, data);
    if (data && e == Q2_OK)
        e = q2_data_component(data, &component);
    if (data && e == Q2_OK)
        e = q2_component_register(kernel, component);
    if (e == Q2_OK)
        e = q2_kernel_boot(kernel);
    if (data && e == Q2_OK)
        e = q2_pc_device_data_poll(&device_data);
    if (e == Q2_OK)
        e = q2_lua_create(kernel, &runtime);
    if (e == Q2_OK && !apps)
        e = q2_lua_start(runtime, json, json_size, source, source_size, grants, grant_count);
    if (e == Q2_OK && apps)
        e = q2_apps_bind(apps, runtime);
    if (e == Q2_OK && apps)
        e = q2_apps_request(apps, manifest.id);
    if (e == Q2_OK && apps)
        e = q2_apps_advance(apps, Q2_OK);
    if (Q2_WEB_ENABLED && e == Q2_OK && web_enabled)
        e = q2_pc_web_create(kernel, keys, display, data ? &device_data : NULL, web_port, &web);
    unsigned rendered = 0;
    bool open = true;
    while (e == Q2_OK && open && (!frames || rendered < frames)) {
        if (apps) {
            uint64_t resident, foreground;
            unsigned running;
            e = q2_apps_state(apps, &resident, &foreground, &running);
            if (e == Q2_OK)
                e = q2_lvgl_focus(display, foreground);
            if (e == Q2_OK && keys)
                e = q2_keys_route(keys, resident, foreground);
        }
        if (data && !apps && e == Q2_OK) {
            q2_app_status_t active;
            e = q2_lua_status(runtime, &active);
            if (e == Q2_OK)
                e = q2_data_focus(data, active.instance);
        }
        if (data && e == Q2_OK)
            e = q2_pc_device_data_poll(&device_data);
        /* Wall-clock belongs to the platform. No battery/link backend is registered on PC. */
        unsigned hour = 0, minute = 0;
        bool clock_valid = q2_pc_local_time(&hour, &minute);
        q2_system_ui_status_t system_status = {
            .clock_valid = clock_valid, .hour = hour, .minute = minute, .battery_percent = -1};
        if (e == Q2_OK && (!data || !apps))
            e = q2_lvgl_system_status(display, &system_status);
        if (e == Q2_OK)
            e = q2_pc_window_pump(window, display, &open);
        bool home = false;
        if (e == Q2_OK)
            e = q2_pc_window_take_home(window, &home);
        if (e == Q2_OK && home) {
            bool consumed = false;
            e = q2_lvgl_activity(display, &consumed);
            if (consumed)
                home = false;
        }
        if (e == Q2_OK && home) {
            if (apps) {
                e = q2_apps_request(apps, NULL);
                /* A queued transition already owns this iteration; repeated navigation is harmless. */
                if (e == Q2_ERR_BUSY)
                    e = Q2_OK;
            } else
                open = false;
        }
        if (e == Q2_OK && rendered >= 5 && rendered % 5 == 0 && rendered / 5 - 1 < click_count) {
            unsigned i = rendered / 5 - 1;
            if (clicks[i][0] >= UINT_MAX - 1)
                e = q2_lvgl_scroll(display,
                                   clicks[i][0] == UINT_MAX ? -(int)clicks[i][1] : (int)clicks[i][1]);
            else
                e = q2_pc_window_click(window, clicks[i][0], clicks[i][1]);
        }
        if (e == Q2_OK && web)
            e = q2_pc_web_poll(web);
        if (e == Q2_OK) {
            e = q2_kernel_poll(kernel, 16);
            if (apps)
                e = q2_apps_advance(apps, e);
        }
        q2_app_status_t status;
        if (e == Q2_OK)
            e = q2_lua_status(runtime, &status);
        if (e == Q2_OK && !status.running && !apps)
            break;
        rendered++;
        q2_pc_sleep_ms(8);
    }
    /* Capture before APP teardown: release_app intentionally deletes all widgets. */
    if (e == Q2_OK && capture)
        e = q2_pc_window_capture(window, capture);
    Q2_LOGI(kernel, "x86", "run finished result=%d iterations=%u", e, rendered);
    if (e != Q2_OK && runtime) {
        q2_app_status_t status;
        q2_lua_status(runtime, &status);
        Q2_LOGE(kernel, "x86", "APP failure: %s", status.error);
    }
    q2_result_t cleanup = q2_pc_web_destroy(&web);
    if (cleanup == Q2_OK && apps)
        cleanup = q2_apps_shutdown(apps);
    if (cleanup == Q2_OK && runtime)
        cleanup = q2_lua_destroy(&runtime);
    if (cleanup == Q2_OK && kernel)
        cleanup = q2_kernel_destroy(&kernel);
#if Q2_COMPONENT_AUDIO
    if (cleanup == Q2_OK && audio)
        cleanup = q2_audio_destroy(&audio);
    if (cleanup == Q2_OK && audio_backend)
        cleanup = q2_pc_audio_destroy(&audio_backend);
#endif
#if Q2_COMPONENT_NETWORK
    if (cleanup == Q2_OK && network)
        cleanup = q2_network_destroy(&network);
    if (cleanup == Q2_OK && transport)
        cleanup = q2_pc_network_destroy(&transport);
#endif
    if (cleanup == Q2_OK && keys)
        cleanup = q2_keys_destroy(&keys);
    if (cleanup == Q2_OK && data)
        cleanup = q2_data_destroy(&data);
    if (cleanup == Q2_OK && apps)
        cleanup = q2_apps_destroy(&apps);
    if (cleanup == Q2_OK && display)
        cleanup = q2_lvgl_destroy(&display);
#if Q2_COMPONENT_STORAGE
    if (cleanup == Q2_OK)
        cleanup = q2_pc_storage_close(&storage);
#endif
#if Q2_COMPONENT_SQLITE
    if (cleanup == Q2_OK && database)
        cleanup = q2_sqlite_destroy(&database);
#endif
    if (cleanup == Q2_OK && window)
        cleanup = q2_pc_window_destroy(&window);
    free(json);
    free(source);
    if (e == Q2_OK)
        e = cleanup;
    const char *name;
    q2_result_name(e, &name);
    /* CLI failures stay actionable even when optional diagnostics are disabled. */
    if (e != Q2_OK)
        fprintf(stderr, "Q2 x86 failed: %s (%d)\n", name, e);
    return e == Q2_OK ? 0 : 1;
}
