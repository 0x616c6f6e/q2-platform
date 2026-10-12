/* Exercise the actual flash catalog/Lua/assets with no physical keys, storage or SDK stubs. */
#include "log_console.h"
#include "q2_factory_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t ticks;
static unsigned flushes;
static bool full_refresh;
static uint32_t frame[Q2_FACTORY_WIDTH * Q2_FACTORY_HEIGHT];
static q2_kernel_t *kernel;
static q2_apps_t *apps;
static q2_lvgl_t *display;
static void *allocate(void *ctx, size_t size) {
    (void)ctx;
    return malloc(size);
}
static void release(void *ctx, void *p) {
    (void)ctx;
    free(p);
}
static uint64_t now(void *ctx) {
    (void)ctx;
    return ticks;
}
static q2_result_t flush(void *ctx, unsigned x, unsigned y, unsigned w, unsigned h, const uint32_t *pixels,
                         size_t stride) {
    (void)ctx;
    assert(x + w <= Q2_FACTORY_WIDTH && y + h <= Q2_FACTORY_HEIGHT);
    assert(stride >= w * sizeof(*pixels));
    if (full_refresh)
        assert(x == 0 && y == 0 && w == Q2_FACTORY_WIDTH && h == Q2_FACTORY_HEIGHT);
    for (unsigned row = 0; row < h; row++)
        memcpy(frame + (y + row) * Q2_FACTORY_WIDTH + x, (const unsigned char *)pixels + row * stride,
               w * sizeof(*pixels));
    flushes++;
    return Q2_OK;
}
static unsigned state(uint64_t *resident, uint64_t *foreground) {
    unsigned running;
    assert(q2_apps_state(apps, resident, foreground, &running) == Q2_OK);
    return running;
}
static void poll(unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        uint64_t resident, foreground;
        state(&resident, &foreground);
        assert(q2_lvgl_focus(display, foreground) == Q2_OK);
        ticks += 32;
        assert(q2_apps_advance(apps, q2_kernel_poll(kernel, 16)) == Q2_OK);
    }
}
static void click(int x, int y) {
    assert(q2_lvgl_pointer(display, x, y, true) == Q2_OK);
    poll(3);
    assert(q2_lvgl_pointer(display, x, y, false) == Q2_OK);
    poll(5);
}
static void capture(const char *path) {
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%u %u\n255\n", Q2_FACTORY_WIDTH, Q2_FACTORY_HEIGHT);
    for (unsigned i = 0; i < Q2_FACTORY_WIDTH * Q2_FACTORY_HEIGHT; i++) {
        unsigned char rgb[] = {frame[i] >> 16, frame[i] >> 8, frame[i]};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    fclose(file);
}
int main(int argc, char **argv) {
    full_refresh = argc > 1 && strcmp(argv[1], "full") == 0;
    const q2_port_t port = {Q2_PORT_ABI, sizeof(port), NULL, allocate, release, now};
    const q2_pixel_backend_t pixels = {Q2_PIXEL_ABI,     sizeof(pixels),    NULL,
                                       Q2_FACTORY_WIDTH, Q2_FACTORY_HEIGHT, flush};
    const q2_component_t *component;
    q2_lua_runtime_t *runtime = NULL;
    assert(q2_kernel_create(&port, &kernel) == Q2_OK);
    assert(q2_console_log_attach(kernel, Q2_LOG_INFO) == Q2_OK);
    assert(q2_lvgl_create(kernel, &pixels, &display) == Q2_OK);
    assert(q2_lvgl_full_refresh(NULL, true) == Q2_ERR_ARGUMENT);
    assert(q2_lvgl_full_refresh(display, true) == Q2_OK);
    assert(q2_lvgl_full_refresh(display, false) == Q2_OK);
    assert(q2_lvgl_full_refresh(display, full_refresh) == Q2_OK);
    const q2_system_ui_config_t ui = {true, false, 32, 30000, 10000};
    const q2_lifecycle_config_t shell = {.status_left = "Q2",
                                         .status_right = "{battery}",
                                         .saver_text = "{time}",
                                         .unlock_key = "POWER",
                                         .unlock_action = 2};
    assert(q2_lvgl_system_configure(display, &ui) == Q2_OK);
    assert(q2_lvgl_lifecycle(display, q2_factory_home_id, &shell) == Q2_OK);
    assert(q2_lvgl_component(display, &component) == Q2_OK);
    assert(q2_component_register(kernel, component) == Q2_OK);
    q2_embedded_catalog_t catalog = {display, q2_factory_home_id, q2_factory_packages,
                                     q2_factory_package_count};
    assert(q2_apps_create(kernel, q2_embedded_prepare, &catalog, &apps) == Q2_OK);
    assert(q2_embedded_register(&catalog, apps) == Q2_OK);
    assert(q2_apps_component(apps, &component) == Q2_OK);
    assert(q2_component_register(kernel, component) == Q2_OK);
    assert(q2_kernel_boot(kernel) == Q2_OK);
    assert(q2_lvgl_full_refresh(display, false) == Q2_ERR_STATE);
    assert(q2_lua_create(kernel, &runtime) == Q2_OK);
    assert(q2_apps_bind(apps, runtime) == Q2_OK);
    assert(q2_apps_request(apps, NULL) == Q2_OK);
    assert(q2_apps_advance(apps, Q2_OK) == Q2_OK);
    const q2_system_ui_status_t status = {.battery_percent = -1};
    assert(q2_lvgl_system_status(display, &status) == Q2_OK);
    poll(8);
    uint64_t resident, foreground;
    assert(state(&resident, &foreground) == 1 && foreground == resident);
    uint64_t original = resident;
    capture("homepage.ppm");
    /* Three complete touch navigation cycles detect leaked images or accidentally restarted home. */
    for (unsigned i = 0; i < 3; i++) {
        click(Q2_FACTORY_WIDTH / 2, 32 + (Q2_FACTORY_HEIGHT - 32) * 43 / 100);
        assert(state(&resident, &foreground) == 2 && resident == original && foreground != resident);
        if (i == 0) {
            capture("settings.ppm");
            assert(q2_lvgl_scroll(display, -140) == Q2_OK);
            poll(10);
            capture("settings-scrolled.ppm");
            assert(q2_lvgl_scroll(display, 140) == Q2_OK);
            poll(10);
        }
        click(Q2_FACTORY_WIDTH - 64, 32 + 35);
        assert(state(&resident, &foreground) == 1 && resident == original && foreground == resident);
    }
    /* Default disabled screensaver must not blank a verified board after idle time. */
    poll(1100);
    assert(state(&resident, &foreground) == 1 && resident == original);
    assert(flushes > 0);
    assert(q2_apps_shutdown(apps) == Q2_OK);
    assert(q2_lua_destroy(&runtime) == Q2_OK);
    assert(q2_kernel_destroy(&kernel) == Q2_OK);
    assert(q2_apps_destroy(&apps) == Q2_OK);
    assert(q2_lvgl_destroy(&display) == Q2_OK);
    puts("PASS: embedded Homepage/Settings touch navigation, resident lifetime, assets, idle and teardown");
    return 0;
}
