/* One variable at a time: clock and pixel bits, identical init/brightness/geometry in every case. */
#include "am178_blue_test.h"
#include "am178.h"
#include "am178_config.h"
#include "am178_pixel.h"
#include "am178_geometry.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void q2_am178_blue_demo(unsigned width, unsigned height, unsigned rotation) {
    const char *tag = "blue_test";
    /* Every case uses the same configured startup brightness; never impose a second test default. */
    ESP_LOGI(tag, "BLUE_TEST_V7 QSPI %s bpp=%u COLMOD=0x%02x brightness=%u%%",
             Q2_AM178_PIXEL_NAME, Q2_AM178_PIXEL_BITS, Q2_AM178_COLMOD,
             (unsigned)Q2_AM178_INITIAL_BRIGHTNESS);
    ESP_LOGI(tag, "%u cases, %u colors, %u clock group(s); quad=1 RAMWR=0x32002c00",
             Q2_AM178_BLUE_STEPS, Q2_AM178_BLUE_LEVELS, Q2_AM178_BLUE_CLOCK_GROUPS);
    ESP_LOGI(tag, "Each case resets once: reference=2000ms test=4000ms recovery=2000ms");
    ESP_LOGI(tag, "Baseline rgb=%06lx; center drawn only in test/recovery stages",
             (unsigned long)Q2_AM178_BLUE_BACKGROUND);
    q2_am178_t *panel = NULL;
    q2_pixel_backend_t backend = {0};
    q2_am178_area_step_t step = {0};
    q2_result_t e = Q2_OK;
    const char *phase = "setup";
    unsigned index = 0, clock_hz = 0;
    for (; index < Q2_AM178_BLUE_STEPS; ++index) {
        phase = "cleanup previous case";
        e = q2_am178_destroy(&panel);
        if (e != Q2_OK)
            break;
        e = q2_am178_blue_step(index, width, height, &step, &clock_hz);
        if (e != Q2_OK)
            break;
        uint8_t wire[Q2_AM178_PIXEL_BYTES];
        q2_am178_pack_pixel(wire, step.rgb);
        ESP_LOGI(tag, "BLUE_CASE %u/%u clock=%uHz rgb=%06lx format=%s rect=%u,%u %ux%u wire_bytes=%u",
                 index + 1u, Q2_AM178_BLUE_STEPS, clock_hz, (unsigned long)step.rgb,
                 Q2_AM178_PIXEL_NAME, step.x, step.y, step.width, step.height, Q2_AM178_PIXEL_BYTES);
        ESP_LOG_BUFFER_HEX_LEVEL(tag, wire, sizeof(wire), ESP_LOG_INFO);
        /* Recreate IO to actually change SCLK. Hardware reset isolates any latched failure from the last case. */
        phase = "panel create/reset/init";
        e = q2_am178_create_with_clock(width, height, rotation, clock_hz, &panel);
        if (e != Q2_OK)
            break;
        e = q2_am178_backend(panel, &backend);
        if (e != Q2_OK)
            break;
        phase = "reference baseline";
        e = q2_am178_blue_baseline(&backend);
        if (e != Q2_OK)
            break;
        ESP_LOGI(tag, "BLUE_REFERENCE case=%u rgb=%06lx: no center overlay; observe 2000ms",
                 index + 1u, (unsigned long)Q2_AM178_BLUE_BACKGROUND);
        vTaskDelay(pdMS_TO_TICKS(2000));
        phase = "center test patch";
        e = q2_am178_blue_patch(&backend, &step);
        if (e != Q2_OK)
            break;
        ESP_LOGI(tag, "BLUE_HOLD case=%u clock=%uHz rgb=%06lx: DMA complete; observe 4000ms",
                 index + 1u, clock_hz, (unsigned long)step.rgb);
        vTaskDelay(pdMS_TO_TICKS(4000));
        /* No reset or register writes here: test whether ordinary pixel data alone restores the center. */
        phase = "A0 recovery without reset";
        q2_am178_area_step_t recovery = step;
        recovery.rgb = 0x0000a0;
        e = q2_am178_blue_patch(&backend, &recovery);
        if (e != Q2_OK)
            break;
        ESP_LOGI(tag, "BLUE_RECOVERY case=%u: center=A0 BLUE; no reset, no background rewrite; observe 2000ms",
                 index + 1u);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (e == Q2_OK) {
        step.rgb = 0x0000a0;
        phase = "final A0 baseline";
        e = q2_am178_blue_patch(&backend, &step);
        if (e == Q2_OK) {
            ESP_LOGI(tag, "BLUE_TEST complete; holding A0 BLUE; background=%06lx clock=%uHz; no visual PASS",
                     (unsigned long)Q2_AM178_BLUE_BACKGROUND, clock_hz);
            for (;;)
                vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    const char *name = "unknown";
    q2_result_name(e, &name);
    ESP_LOGE(tag, "BLUE_TEST stopped case=%u phase=%s result=%s", index + 1u, phase, name);
    if (q2_am178_destroy(&panel) != Q2_OK)
        ESP_LOGE(tag, "panel cleanup failed; reboot required");
}
