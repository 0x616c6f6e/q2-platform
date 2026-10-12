/* Minimal port bring-up only. Player peripherals remain in the legacy firmware until migrated. */
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_console.h"
#include "q2/kernel.h"
#include "q2/runtime.h"
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
    const q2_port_t port = {Q2_PORT_ABI, sizeof(port), NULL, allocate, release, now};
    q2_kernel_t *kernel = NULL;
    q2_lua_runtime_t *runtime = NULL;
    q2_result_t result = q2_kernel_create(&port, &kernel);
    q2_log_level_t level = Q2_LOG_OFF;
#if Q2_ENABLE_LOG
#ifdef Q2_USE_PROJECT_CONFIG
    level = (q2_log_level_t)Q2_DEFAULT_LOG_LEVEL;
#else
    level = (q2_log_level_t)CONFIG_Q2_LOG_LEVEL;
#endif
#endif
    if (result == Q2_OK)
        result = q2_console_log_attach(kernel, level);
    if (result == Q2_OK)
        result = q2_kernel_boot(kernel);
    if (result == Q2_OK)
        result = q2_lua_create(kernel, &runtime);
    const char *name;
    q2_result_name(result, &name);
    Q2_LOGI(kernel, "q2-port", "Q2-Kernel port: %s", name);
    if (result != Q2_OK) {
        /* Preserve a fatal bring-up diagnostic even when normal logging is disabled. */
        fprintf(stderr, "Q2 port failed: %s\n", name);
        if (kernel)
            q2_kernel_destroy(&kernel);
        return;
    }
    /* One owner task advances the kernel. No scheduler/thread API is required by the core. */
    for (;;) {
        /* Kernel records poll failures through the configured diagnostic sink. */
        (void)q2_kernel_poll(kernel, 8);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
