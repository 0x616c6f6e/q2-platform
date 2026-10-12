/* ESP32-S3 owner task: display/touch providers and resident factory Lua applications. */
#include "am178.h"
#include "embedded_apps.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_console.h"
#include "panel_profile.h"
#include "q2/runtime.h"
#include "q2_factory_bundle.h"
#include "sdkconfig.h"
#ifdef Q2_USE_PROJECT_CONFIG
#include "q2_project_config.h"
#else
#define Q2_DISPLAY_ROTATION CONFIG_Q2_AM178_ROTATION
#if Q2_DISPLAY_ROTATION == 90 || Q2_DISPLAY_ROTATION == 270
#define Q2_DISPLAY_WIDTH Q2_PANEL_NATIVE_HEIGHT
#define Q2_DISPLAY_HEIGHT Q2_PANEL_NATIVE_WIDTH
#else
#define Q2_DISPLAY_WIDTH Q2_PANEL_NATIVE_WIDTH
#define Q2_DISPLAY_HEIGHT Q2_PANEL_NATIVE_HEIGHT
#endif
#define Q2_DEFAULT_LOG_LEVEL CONFIG_Q2_LOG_LEVEL
#endif
#include "q2_platform_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *allocate(void *ctx, size_t size) {
    (void)ctx;
    return malloc(size);
}
static void release(void *ctx, void *memory) {
    (void)ctx;
    free(memory);
}
static uint64_t now(void *ctx) {
    (void)ctx;
    return (uint64_t)esp_timer_get_time() / 1000;
}
void app_main(void) {
    /* Large LVGL allocations use IDF malloc/PSRAM; DMA tiles explicitly remain in internal RAM.
     * Fail clearly rather than booting a display that will exhaust internal SRAM. */
    if (!esp_psram_is_initialized() || esp_psram_get_size() < 8 * 1024 * 1024) {
        fprintf(stderr, "Q2 AMOLED profile requires at least 8 MiB PSRAM; check module/sdkconfig\n");
        return;
    }
    if (Q2_DISPLAY_WIDTH != Q2_FACTORY_WIDTH || Q2_DISPLAY_HEIGHT != Q2_FACTORY_HEIGHT) {
        fprintf(stderr, "Factory asset resolution does not match the display configuration\n");
        return;
    }
    q2_kernel_t *kernel = NULL;
    q2_lua_runtime_t *runtime = NULL;
    q2_apps_t *apps = NULL;
    q2_embedded_catalog_t catalog = {0};
    q2_lvgl_t *display = NULL;
    q2_am178_t *panel = NULL;
    q2_pixel_backend_t pixels;
    const q2_component_t *component;
    q2_component_t panel_component;
    const q2_port_t port = {Q2_PORT_ABI, sizeof(port), NULL, allocate, release, now};
    q2_result_t e = q2_kernel_create(&port, &kernel);
    q2_log_level_t level = Q2_LOG_OFF;
#if Q2_ENABLE_LOG
    level = (q2_log_level_t)Q2_DEFAULT_LOG_LEVEL;
#endif
    if (e == Q2_OK)
        e = q2_console_log_attach(kernel, level);
    if (e == Q2_OK)
        e = q2_am178_create(Q2_DISPLAY_WIDTH, Q2_DISPLAY_HEIGHT, Q2_DISPLAY_ROTATION, &panel);
    if (e == Q2_OK)
        e = q2_am178_backend(panel, &pixels);
    if (e == Q2_OK)
        e = q2_lvgl_create(kernel, &pixels, &display);
    /* Complete the logical composition before rotating it into native panel rows. The PSRAM
     * draw buffer avoids hundreds of narrow windows from a 90/270-degree partial refresh. */
    if (e == Q2_OK)
        e = q2_lvgl_full_refresh(display, true);
    if (e == Q2_OK)
        Q2_LOGI(kernel, "esp32s3", "UI refresh=full-frame draw_bytes=%u native_row_DMA=blocking",
                (unsigned)(Q2_DISPLAY_WIDTH * Q2_DISPLAY_HEIGHT * sizeof(uint32_t)));
    const q2_system_ui_config_t system_ui = {Q2_STATUS_BAR_ENABLED, Q2_SCREENSAVER_ENABLED,
                                             Q2_STATUS_BAR_HEIGHT, Q2_IDLE_TIMEOUT_MS,
                                             Q2_CLOCK_MOVE_INTERVAL_MS};
    const q2_lifecycle_config_t lifecycle = {.lock_enabled = Q2_LOCK_ENABLED,
                                             .status_left = Q2_STATUS_LEFT,
                                             .status_right = Q2_STATUS_RIGHT,
                                             .saver_text = Q2_SAVER_TEXT,
                                             .unlock_key = Q2_UNLOCK_KEY,
                                             .unlock_action = Q2_UNLOCK_ACTION};
    if (e == Q2_OK)
        e = q2_lvgl_system_configure(display, &system_ui);
    if (e == Q2_OK)
        e = q2_lvgl_lifecycle(display, q2_factory_home_id, &lifecycle);
    if (e == Q2_OK)
        e = q2_lvgl_component(display, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
    if (e == Q2_OK)
        e = q2_am178_controls(panel, &panel_component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, &panel_component);
    /* Package bytes live in flash; Kernel copies manifests/Lua, images are prepared per APP.
     * No SD card, writable filesystem or network is needed to reach the home screen. */
    catalog =
        (q2_embedded_catalog_t){display, q2_factory_home_id, q2_factory_packages, q2_factory_package_count};
    if (e == Q2_OK)
        e = q2_apps_create(kernel, q2_embedded_prepare, &catalog, &apps);
    if (e == Q2_OK)
        e = q2_embedded_register(&catalog, apps);
    if (e == Q2_OK)
        e = q2_apps_component(apps, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
    if (e == Q2_OK)
        e = q2_kernel_boot(kernel);
    if (e == Q2_OK)
        e = q2_lua_create(kernel, &runtime);
    if (e == Q2_OK)
        e = q2_apps_bind(apps, runtime);
    if (e == Q2_OK)
        e = q2_apps_request(apps, NULL);
    if (e == Q2_OK)
        e = q2_apps_advance(apps, Q2_OK);
    Q2_LOGI(kernel, "esp32s3", "%s %ux%u, PSRAM free=%u, internal free=%u", Q2_PANEL_NAME, Q2_DISPLAY_WIDTH,
            Q2_DISPLAY_HEIGHT, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    bool was_pressed = false;
    int last_x = 0, last_y = 0;
    while (e == Q2_OK) {
        uint64_t resident, foreground;
        unsigned running;
        e = q2_apps_state(apps, &resident, &foreground, &running);
        if (e == Q2_OK)
            e = q2_lvgl_focus(display, foreground);
        /* No RTC/network/battery provider is attached yet: publish unknown, not simulated values. */
        const q2_system_ui_status_t status = {.clock_valid = false, .battery_percent = -1};
        if (e == Q2_OK)
            e = q2_lvgl_system_status(display, &status);
        /* Replay captured edges in order before polling Lua/rendering. An empty FIFO is not a release.
         * Bound each batch so neither input nor drawing can starve the other. */
        for (unsigned sample = 0; sample < 16 && e == Q2_OK; sample++) {
            bool pressed;
            int x, y;
            q2_result_t read = q2_am178_touch(panel, &pressed, &x, &y);
            if (read == Q2_ERR_NOT_FOUND || read == Q2_ERR_UNSUPPORTED)
                break;
            if (read != Q2_OK)
                pressed = false;
            if (pressed || was_pressed) {
                e = q2_lvgl_pointer(display, pressed ? x : last_x, pressed ? y : last_y, pressed);
                if (pressed) {
                    last_x = x;
                    last_y = y;
                }
            }
            was_pressed = pressed;
        }
        if (e == Q2_OK)
            e = q2_apps_advance(apps, q2_kernel_poll(kernel, 16));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    const char *name = "unknown";
    q2_result_name(e, &name);
    fprintf(stderr, "Q2 ESP32-S3 stopped: %s\n", name);
    if (runtime) {
        q2_app_status_t status;
        if (q2_lua_status(runtime, &status) == Q2_OK)
            fprintf(stderr, "Lua: %s\n", status.error);
    }
    /* Preserve dependencies if cleanup fails; no dangling backend pointers. Reboot to recover. */
    /* A failed registration may precede runtime creation. Destroy only acquired resources. */
    q2_result_t cleanup = apps ? q2_apps_shutdown(apps) : Q2_OK;
    if (cleanup == Q2_OK && runtime)
        cleanup = q2_lua_destroy(&runtime);
    if (cleanup == Q2_OK && kernel)
        cleanup = q2_kernel_destroy(&kernel);
    if (cleanup == Q2_OK && apps)
        cleanup = q2_apps_destroy(&apps);
    if (cleanup == Q2_OK && display)
        cleanup = q2_lvgl_destroy(&display);
    if (cleanup == Q2_OK && panel)
        cleanup = q2_am178_destroy(&panel);
    if (cleanup != Q2_OK)
        fprintf(stderr, "Q2 cleanup failed; reboot required\n");
}
