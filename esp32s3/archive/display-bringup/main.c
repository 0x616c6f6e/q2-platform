/* ESP32-S3 owner task: registered portable LVGL + isolated Lua display/touch fixture. */
#include "am178.h"
#include "panel_profile.h"
#include "am178_area_test.h"
#include "am178_blue_test.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_console.h"
#include "q2/runtime.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const char demo_start[] asm("_binary_display_demo_lua_start");
static const char manifest[] =
    "{\"format\":2,\"id\":\"a6014dd9-ce1f-46b9-9e77-260d837df49e\","
    "\"name\":\"AMOLED RGB cycle\",\"version\":\"1.0.0\",\"entry\":\"main.lua\","
    "\"type\":\"app\",\"api\":2,\"capabilities\":{\"required\":[\"display.ui\",\"display.brightness\"],\"optional\":[\"system.log\"]},"
    "\"resources\":{\"lua_heap_kib\":64}}";
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
#ifdef CONFIG_Q2_AM178_DIRECT_RGB
static void direct_rgb_demo(void) {
    q2_am178_t *panel = NULL;
    q2_result_t e = q2_am178_create(Q2_DISPLAY_WIDTH, Q2_DISPLAY_HEIGHT, Q2_DISPLAY_ROTATION, &panel);
    const uint32_t colors[] = {0xa00000, 0x00a000, 0x0000a0};
    const char *names[] = {"RED", "GREEN", "BLUE"};
    unsigned index = 0;
    uint32_t step = 0;
    ESP_LOGI("rgb_test", "DIRECT RGB mode; no Lua, LVGL or touch; 2s per color");
    while (e == Q2_OK) {
        ESP_LOGI("rgb_test", "color=%s step=%lu", names[index], (unsigned long)++step);
        e = q2_am178_solid_rgb(panel, colors[index]);
        if (e == Q2_OK) {
            index = (index + 1) % 3;
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    const char *name = "unknown";
    q2_result_name(e, &name);
    ESP_LOGE("rgb_test", "DIRECT RGB stopped: %s", name);
    if (q2_am178_destroy(&panel) != Q2_OK)
        ESP_LOGE("rgb_test", "panel cleanup failed; reboot required");
}
#endif
#ifdef CONFIG_Q2_AM178_AREA_TEST
static void area_demo(void) {
    q2_am178_t *panel = NULL;
    q2_pixel_backend_t backend;
    q2_am178_area_step_t step = {0};
    unsigned brightness = 0, index = 0;
    const char *phase = "setup";
    q2_result_t e = q2_am178_create(Q2_DISPLAY_WIDTH, Q2_DISPLAY_HEIGHT, Q2_DISPLAY_ROTATION, &panel);
    if (e == Q2_OK)
        e = q2_am178_backend(panel, &backend);
    if (e == Q2_OK)
        e = q2_am178_brightness(panel, false, &brightness);
    ESP_LOGI("area_test", "AREA_TEST start steps=%u brightness_cached=%u%% hold_ms=3000 black_ms=1000",
             Q2_AM178_AREA_STEPS, brightness);
    for (; index < Q2_AM178_AREA_STEPS && e == Q2_OK; ++index) {
        e = q2_am178_area_step(index, backend.width, backend.height, &step);
        if (e != Q2_OK)
            break;
        unsigned coverage = (unsigned)((uint64_t)step.width * step.height * 10000u /
                                        ((uint64_t)backend.width * backend.height));
        ESP_LOGI("area_test", "AREA_STEP %u/%u %s rgb=%06lx rect=%u,%u %ux%u area=%u.%02u%% target=%u%%",
                 index + 1u, Q2_AM178_AREA_STEPS, step.color_name, (unsigned long)step.rgb,
                 step.x, step.y, step.width, step.height, coverage / 100u, coverage % 100u,
                 step.target_percent);
        /* Black rest clears the prior rectangle. Both black and color send a full frame,
         * so area changes do not also change the number/size of DMA transactions. */
        q2_am178_area_step_t black = step;
        black.rgb = 0;
        phase = "black";
        e = q2_am178_area_render(&backend, &black);
        if (e != Q2_OK)
            break;
        vTaskDelay(pdMS_TO_TICKS(1000));
        phase = "color";
        e = q2_am178_area_render(&backend, &step);
        if (e != Q2_OK)
            break;
        ESP_LOGI("area_test", "AREA_HOLD step=%u DMA complete; observe panel for 3000ms", index + 1u);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    if (e == Q2_OK) {
        /* A visible known working baseline distinguishes test completion from an idle black screen. */
        step = (q2_am178_area_step_t){.width = backend.width, .height = backend.height, .rgb = 0x0000a0};
        phase = "final baseline";
        e = q2_am178_area_render(&backend, &step);
        if (e == Q2_OK) {
            ESP_LOGI("area_test", "AREA_TEST complete; holding full-screen A0 BLUE; reset to repeat");
            for (;;)
                vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    const char *name = "unknown";
    q2_result_name(e, &name);
    ESP_LOGE("area_test", "AREA_TEST stopped step=%u phase=%s result=%s", index + 1u, phase, name);
    if (q2_am178_destroy(&panel) != Q2_OK)
        ESP_LOGE("area_test", "panel cleanup failed; reboot required");
}
#endif
void app_main(void) {
    /* Large LVGL allocations use IDF malloc/PSRAM; DMA tiles explicitly remain in internal RAM.
     * Fail clearly rather than booting a display that will exhaust internal SRAM. */
    if (!esp_psram_is_initialized() || esp_psram_get_size() < 8 * 1024 * 1024) {
        fprintf(stderr, "Q2 AMOLED profile requires at least 8 MiB PSRAM; check module/sdkconfig\n");
        return;
    }
#if defined(CONFIG_Q2_AM178_BLUE_TEST)
    q2_am178_blue_demo(Q2_DISPLAY_WIDTH, Q2_DISPLAY_HEIGHT, Q2_DISPLAY_ROTATION);
    return;
#elif defined(CONFIG_Q2_AM178_AREA_TEST)
    area_demo();
    return;
#elif defined(CONFIG_Q2_AM178_DIRECT_RGB)
    direct_rgb_demo();
    return;
#endif
    q2_kernel_t *kernel = NULL;
    q2_lua_runtime_t *runtime = NULL;
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
    /* This isolated RGB fixture must never create native saver/status overlays or a lifecycle APP.
     * Configure explicitly rather than relying on calloc defaults or the desktop product profile. */
    const q2_system_ui_config_t diagnostic_ui = {.status_bar = false, .screensaver = false};
    if (e == Q2_OK)
        e = q2_lvgl_system_configure(display, &diagnostic_ui);
    if (e == Q2_OK)
        e = q2_lvgl_component(display, &component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, component);
    if (e == Q2_OK)
        e = q2_am178_controls(panel, &panel_component);
    if (e == Q2_OK)
        e = q2_component_register(kernel, &panel_component);
    if (e == Q2_OK)
        e = q2_kernel_boot(kernel);
    if (e == Q2_OK)
        e = q2_lua_create(kernel, &runtime);
    const char *grants[] = {"display.ui", "display.brightness", "system.log"};
    if (e == Q2_OK)
        e = q2_lua_start(runtime, manifest, strlen(manifest), demo_start, strlen(demo_start), grants, 3);
    Q2_LOGI(kernel, "esp32s3", "%s %ux%u, PSRAM free=%u, internal free=%u", Q2_PANEL_NAME, Q2_DISPLAY_WIDTH,
            Q2_DISPLAY_HEIGHT, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    bool was_pressed = false;
    int last_x = 0, last_y = 0;
    while (e == Q2_OK) {
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
            e = q2_kernel_poll(kernel, 16);
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
    q2_result_t cleanup = runtime ? q2_lua_destroy(&runtime) : Q2_OK;
    if (cleanup == Q2_OK && kernel)
        cleanup = q2_kernel_destroy(&kernel);
    if (cleanup == Q2_OK && display)
        cleanup = q2_lvgl_destroy(&display);
    if (cleanup == Q2_OK && panel)
        cleanup = q2_am178_destroy(&panel);
    if (cleanup != Q2_OK)
        fprintf(stderr, "Q2 cleanup failed; reboot required\n");
}
