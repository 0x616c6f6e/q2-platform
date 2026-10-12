#include "am178.h"
#include "am178_geometry.h"
#include "am178_pixel.h"
#include "am178_touch.h"
#include "am178_vendor_init.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define TILE_ROWS Q2_LCD_TILE_ROWS
#define TILE_BYTES (Q2_AM178_WIDTH * TILE_ROWS * Q2_AM178_PIXEL_BYTES)
#define FRAME_BYTES (Q2_AM178_WIDTH * Q2_AM178_HEIGHT * Q2_AM178_PIXEL_BYTES)
#define TOUCH_SAMPLES 32u
#define TOUCH_EDGE_RESERVE 8u
typedef struct {
    bool pressed;
    int x, y;
    q2_result_t result;
} touch_sample_t;
static const char *TAG = Q2_PANEL_NAME;
struct q2_am178 {
    esp_lcd_panel_io_handle_t lcd_io;
    esp_lcd_panel_handle_t lcd_panel;
    SemaphoreHandle_t lcd_done;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t touch;
    uint8_t *frame;
    uint8_t *tile;
    unsigned width, height, rotation, brightness, clock_hz;
    uint64_t touch_reset_low_ms;
    bool spi_bus, failed, touch_reported;
    QueueHandle_t touch_events, touch_samples;
    TaskHandle_t touch_task;
    SemaphoreHandle_t touch_stopped;
    atomic_bool touch_stop;
    bool touch_irq, touch_irq_service;
    q2_am178_touch_state_t touch_state;
    esp_err_t touch_last_error;
    uint64_t touch_log_at_ms;
};
static q2_result_t result(esp_err_t e) {
    return e == ESP_OK            ? Q2_OK
           : e == ESP_ERR_NO_MEM  ? Q2_ERR_MEMORY
           : e == ESP_ERR_TIMEOUT ? Q2_ERR_TIMEOUT
                                  : Q2_ERR_IO;
}
/* The selected panel component owns command encoding, initialization and draw windows.
 * No hardware swap_xy/mirror API is called; logical rotation stays in am178_geometry.c. */
static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context) {
    (void)io;
    (void)event;
    q2_am178_t *s = context;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(s->lcd_done, &wake);
    /* This IDF SPI IO backend discards the callback's bool return. Request the context switch
     * here so every DMA band does not wait for the next 100 Hz tick or unrelated touch IRQ.
     * Return false because the ISR yield has already been requested. */
    if (wake == pdTRUE)
        portYIELD_FROM_ISR();
    return false;
}
/* Driver brightness units differ: CO5300 takes percent; AM178 helper takes the raw register. */
static esp_err_t set_panel_brightness(q2_am178_t *s, unsigned percent) {
#if Q2_PANEL_AM213
    return esp_lcd_panel_co5300_set_brightness(s->lcd_panel, (uint8_t)percent);
#else
    return panel_qspi_amoled_set_brightness(s->lcd_panel, (uint8_t)Q2_AM178_BRIGHTNESS_REGISTER(percent));
#endif
}
/* Owner-only register access, serialized with pixel transfers. Cache is not a panel readback. */
q2_result_t q2_am178_brightness(q2_am178_t *s, bool write, unsigned *percent) {
    if (!s || !percent || (write && *percent > 100))
        return Q2_ERR_ARGUMENT;
    if (s->failed)
        return Q2_ERR_STATE;
    if (write) {
        uint8_t value = (uint8_t)Q2_AM178_BRIGHTNESS_REGISTER(*percent);
        esp_err_t e = set_panel_brightness(s, *percent);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "PARAM failed cmd=0x51 old=0x%02x requested=0x%02x (%u%% -> %u%%): %s",
                     (unsigned)Q2_AM178_BRIGHTNESS_REGISTER(s->brightness), (unsigned)value, s->brightness,
                     *percent, esp_err_to_name(e));
            return result(e);
        }
        ESP_LOGI(TAG, "PARAM applied cmd=0x51 old=0x%02x new=0x%02x (%u%% -> %u%%)",
                 (unsigned)Q2_AM178_BRIGHTNESS_REGISTER(s->brightness), (unsigned)value, s->brightness,
                 *percent);
        s->brightness = *percent;
    }
    *percent = s->brightness;
    return Q2_OK;
}
static esp_err_t draw_band(q2_am178_t *s, unsigned x, unsigned y, unsigned w, unsigned h) {
    esp_err_t e = esp_lcd_panel_draw_bitmap(s->lcd_panel, x, y, x + w, y + h, s->tile);
    /* The vendor draw API is asynchronous. Keep Q2's synchronous backend contract by waiting
     * for the callback before returning or reusing the internal DMA tile. */
    if (e == ESP_OK && xSemaphoreTake(s->lcd_done, pdMS_TO_TICKS(1000)) != pdTRUE)
        e = ESP_ERR_TIMEOUT;
    if (e != ESP_OK)
        s->failed = true;
    /* Window updates are high-frequency. Keep per-transfer logging at DEBUG, after completion. */
    ESP_LOGD(TAG, "WINDOW cmd=0x2A x=%u..%u cmd=0x2B y=%u..%u RAMWR bytes=%u result=%s", x + Q2_AM178_X_GAP,
             x + Q2_AM178_X_GAP + w - 1u, y, y + h - 1u, w * h * Q2_AM178_PIXEL_BYTES, esp_err_to_name(e));
    return e;
}
static q2_result_t flush(void *ctx, unsigned x, unsigned y, unsigned w, unsigned h, const uint32_t *pixels,
                         size_t stride) {
    q2_am178_t *s = ctx;
    if (!pixels || !w || !h || x >= s->width || y >= s->height || w > s->width - x || h > s->height - y ||
        stride < w * 4 || stride % 4)
        return Q2_ERR_ARGUMENT;
    if (s->failed)
        return Q2_ERR_STATE;
    unsigned left = Q2_AM178_WIDTH, top = Q2_AM178_HEIGHT, right = 0, bottom = 0;
    for (unsigned j = 0; j < h; ++j) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + j * stride);
        for (unsigned i = 0; i < w; ++i) {
            unsigned nx, ny;
            q2_am178_native(s->width, s->height, s->rotation, x + i, y + j, &nx, &ny);
            q2_am178_pack_pixel(s->frame + (ny * Q2_AM178_WIDTH + nx) * Q2_AM178_PIXEL_BYTES, row[i]);
            if (nx < left)
                left = nx;
            if (nx > right)
                right = nx;
            if (ny < top)
                top = ny;
            if (ny > bottom)
                bottom = ny;
        }
    }
    /* AMOLED windows must have even origin/extent. Preserve neighboring pixels from
     * the shadow framebuffer instead of reading outside the caller's dirty rectangle. */
    left &= ~1u;
    top &= ~1u;
    right |= 1u;
    bottom |= 1u;
    unsigned width = right - left + 1;
    for (unsigned yy = top; yy <= bottom; yy += TILE_ROWS) {
        unsigned rows = bottom - yy + 1;
        if (rows > TILE_ROWS)
            rows = TILE_ROWS;
        /* Shadow pixels are already in wire order for the selected format. */
        for (unsigned j = 0; j < rows; ++j)
            q2_am178_copy_pixel_row(s->tile + j * width * Q2_AM178_PIXEL_BYTES, s->frame, left, yy + j,
                                    width);
        esp_err_t e = draw_band(s, left, yy, width, rows);
        if (e != ESP_OK) {
            s->failed = true;
            ESP_LOGE(TAG, "QSPI flush failed: %s", esp_err_to_name(e));
            return result(e);
        }
    }
    return Q2_OK;
}
/* Dump the real table only after the vendor init returned success: all listed transfers
 * completed at the host. This is not register readback. A failed init never logs the table as applied. */
static void log_applied_init(void) {
    size_t count = sizeof(q2_am178_vendor_init_cmds) / sizeof(q2_am178_vendor_init_cmds[0]);
    ESP_LOGD(TAG, "INIT applied driver prefix: RGB/%s (MADCTL=0x00 COLMOD=0x%02x)", Q2_AM178_PIXEL_NAME,
             Q2_AM178_COLMOD);
    for (size_t i = 0; i < count; ++i) {
        const q2_panel_init_cmd_t *cmd = &q2_am178_vendor_init_cmds[i];
        ESP_LOGD(TAG, "INIT applied [%u/%u] cmd=0x%02x bytes=%u delay_ms=%u", (unsigned)i + 1u,
                 (unsigned)count, (unsigned)cmd->cmd, (unsigned)cmd->data_bytes, cmd->delay_ms);
        if (cmd->data_bytes)
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, cmd->data, cmd->data_bytes, ESP_LOG_DEBUG);
    }
    ESP_LOGD(TAG, "INIT transfers complete; logged values are sent parameters, not panel readback");
}
static esp_err_t panel_init(q2_am178_t *s) {
#if Q2_PANEL_AM213
    co5300_vendor_config_t vendor = {
#else
    qspi_amoled_vendor_config_t vendor = {
#endif
        .init_cmds = q2_am178_vendor_init_cmds,
        .init_cmds_size = sizeof(q2_am178_vendor_init_cmds) / sizeof(q2_am178_vendor_init_cmds[0]),
        .flags.use_qspi_interface = 1
    };
    const esp_lcd_panel_dev_config_t config = {.reset_gpio_num = Q2_LCD_RST,
                                               .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                               .bits_per_pixel = Q2_AM178_PIXEL_BITS,
                                               .vendor_config = &vendor};
#if Q2_PANEL_AM213
    esp_err_t e = esp_lcd_new_panel_co5300(s->lcd_io, &config, &s->lcd_panel);
#else
    esp_err_t e = esp_lcd_new_panel_qspi_amoled(s->lcd_io, &config, &s->lcd_panel);
#endif
    if (e == ESP_OK)
        e = esp_lcd_panel_reset(s->lcd_panel);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "INIT begin: logical=%ux%u rotation=%u QSPI=%uHz tile_rows=%u queue=%u", s->width,
                 s->height, s->rotation, s->clock_hz, TILE_ROWS, Q2_LCD_QUEUE_DEPTH);
        e = esp_lcd_panel_init(s->lcd_panel);
        if (e == ESP_OK)
            log_applied_init();
        else
            ESP_LOGE(TAG, "INIT incomplete: %s; later table entries may not have been sent",
                     esp_err_to_name(e));
    }
    if (e == ESP_OK) {
        e = esp_lcd_panel_set_gap(s->lcd_panel, Q2_AM178_X_GAP, 0);
        ESP_LOGI(TAG, "PARAM software gap=(%u,0) result=%s", Q2_AM178_X_GAP, esp_err_to_name(e));
    }
    /* Preserve the reference's post-init Display On; previously our shortened path omitted it. */
    if (e == ESP_OK) {
        e = esp_lcd_panel_disp_on_off(s->lcd_panel, true);
        ESP_LOGI(TAG, "PARAM display_on cmd=0x29 bytes=0 result=%s", esp_err_to_name(e));
    }
#if Q2_PANEL_AM213
    /* Match the vendor's post-Display-On API call using the same product brightness as the table.
     * This is a percent, not a raw 0..255 argument. No second independent brightness setting. */
    if (e == ESP_OK)
        e = set_panel_brightness(s, Q2_AM178_INITIAL_BRIGHTNESS);
#endif
    return e;
}
static void touch_worker(void *context);
static void IRAM_ATTR touch_interrupt(void *ctx) {
    QueueHandle_t events = ctx;
    uint8_t pending = 1;
    BaseType_t wake = pdFALSE;
    /* Wake the high-priority acquisition task; no I2C or Kernel API runs inside the ISR. */
    xQueueSendFromISR(events, &pending, &wake);
    if (wake)
        portYIELD_FROM_ISR();
}
static esp_err_t touch_init(q2_am178_t *s) {
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = Q2_TP_SDA,
        .scl_io_num = Q2_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* AM213 bare touch IO is 1.8 V: use adapter/external pullups, never ESP 3.3 V pullups. */
        .flags.enable_internal_pullup = !Q2_PANEL_AM213};
    esp_err_t e = i2c_new_master_bus(&bus, &s->bus);
    if (e != ESP_OK)
        return e;
    /* Probe only the selected touch controller; a missing TP must not block display initialization. */
    e = i2c_master_probe(s->bus, Q2_TOUCH_ADDRESS, 50);
    if (e != ESP_OK)
        return e;
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = Q2_TOUCH_ADDRESS, .scl_speed_hz = 100000};
    e = i2c_master_bus_add_device(s->bus, &dev, &s->touch);
    if (e != ESP_OK)
        return e;
    s->touch_events = xQueueCreate(1, sizeof(uint8_t));
    s->touch_samples = xQueueCreate(TOUCH_SAMPLES, sizeof(touch_sample_t));
    s->touch_stopped = xSemaphoreCreateBinary();
    if (!s->touch_events || !s->touch_samples || !s->touch_stopped)
        return ESP_ERR_NO_MEM;
    atomic_init(&s->touch_stop, false);
    /* A dedicated higher-priority task captures reports even while the owner flushes the display. */
    if (xTaskCreate(touch_worker, "q2_touch", 4096, s, 5, &s->touch_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    e = gpio_install_isr_service(0);
    if (e == ESP_OK)
        s->touch_irq_service = true;
    else if (e != ESP_ERR_INVALID_STATE)
        return e;
    e = gpio_set_intr_type(Q2_TP_INT, GPIO_INTR_NEGEDGE);
    if (e == ESP_OK)
        e = gpio_isr_handler_add(Q2_TP_INT, touch_interrupt, s->touch_events);
    if (e != ESP_OK)
        return e;
    s->touch_irq = true;
    /* Account for a report already pending when the falling-edge handler was attached. */
    if (gpio_get_level(Q2_TP_INT) == 0) {
        uint8_t pending = 1;
        xQueueSend(s->touch_events, &pending, 0);
    }
    return ESP_OK;
}
q2_result_t q2_am178_create(unsigned w, unsigned h, unsigned rotation, q2_am178_t **out) {
    const unsigned clock_hz = Q2_LCD_CLOCK_HZ;
    if (!out)
        return Q2_ERR_ARGUMENT;
    *out = NULL;
    if (!q2_am178_dimensions(w, h, rotation))
        return Q2_ERR_ARGUMENT;
    q2_am178_t *s = calloc(1, sizeof(*s));
    if (!s)
        return Q2_ERR_MEMORY;
    s->width = w;
    s->height = h;
    s->rotation = rotation;
    s->clock_hz = clock_hz;
    s->frame = heap_caps_calloc(Q2_AM178_WIDTH * Q2_AM178_HEIGHT, Q2_AM178_PIXEL_BYTES,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s->tile = heap_caps_malloc(TILE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s->lcd_done = xSemaphoreCreateBinary();
    esp_err_t e = s->frame && s->tile && s->lcd_done ? ESP_OK : ESP_ERR_NO_MEM;
    /* The vendor panel exclusively owns LCD_RST; this adapter configures TP_RST only. */
    gpio_config_t output = {.pin_bit_mask = (1ULL << Q2_TP_RST), .mode = GPIO_MODE_OUTPUT};
    gpio_config_t input = {.pin_bit_mask = (1ULL << Q2_LCD_TE) | (1ULL << Q2_TP_INT),
                           .mode = GPIO_MODE_INPUT};
    if (e == ESP_OK)
        e = gpio_config(&output);
    if (e == ESP_OK)
        e = gpio_config(&input);
    if (e == ESP_OK) {
        /* Hold TP in reset; the vendor resets LCD only after constructing its IO device. */
        gpio_set_level(Q2_TP_RST, 0);
        s->touch_reset_low_ms = (uint64_t)esp_timer_get_time() / 1000;
        spi_bus_config_t bus = {.sclk_io_num = Q2_LCD_CLK,
                                .data0_io_num = Q2_LCD_D0,
                                .data1_io_num = Q2_LCD_D1,
                                .data2_io_num = Q2_LCD_D2,
                                .data3_io_num = Q2_LCD_D3,
                                .data4_io_num = -1,
                                .data5_io_num = -1,
                                .data6_io_num = -1,
                                .data7_io_num = -1,
                                .max_transfer_sz = TILE_BYTES,
                                .flags = SPICOMMON_BUSFLAG_QUAD};
        e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
        s->spi_bus = e == ESP_OK;
    }
    if (e == ESP_OK) {
        esp_lcd_panel_io_spi_config_t dev = {.cs_gpio_num = Q2_LCD_CS,
                                             .dc_gpio_num = -1,
                                             .spi_mode = 0,
                                             .pclk_hz = s->clock_hz,
                                             .trans_queue_depth = Q2_LCD_QUEUE_DEPTH,
                                             .lcd_cmd_bits = 32,
                                             .lcd_param_bits = 8,
                                             .on_color_trans_done = color_done,
                                             .user_ctx = s,
                                             .flags.quad_mode = true};
        e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &dev, &s->lcd_io);
        ESP_LOGI(TAG, "TRANSPORT esp-lcd split-header-data DMA=enabled result=%s", esp_err_to_name(e));
    }
    if (e == ESP_OK)
        e = panel_init(s);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "display initialization failed: %s", esp_err_to_name(e));
        q2_am178_destroy(&s);
        return result(e);
    }
    /* Cache becomes valid only after the complete initialization succeeded. */
    s->brightness = Q2_AM178_INITIAL_BRIGHTNESS;
    ESP_LOGI(TAG, "initial brightness=%u%% register=0x%02x", s->brightness,
             (unsigned)Q2_AM178_BRIGHTNESS_REGISTER(s->brightness));
    /* TP is independent: retain its original minimum reset and boot delays. */
    uint64_t held_ms = (uint64_t)esp_timer_get_time() / 1000 - s->touch_reset_low_ms;
    if (held_ms < 200)
        vTaskDelay(pdMS_TO_TICKS((unsigned)(200 - held_ms)) + 1);
    e = gpio_set_level(Q2_TP_RST, 1);
    if (e == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(200));
        e = touch_init(s);
    }
    if (e != ESP_OK)
        ESP_LOGW(TAG, "%s unavailable (%s); display remains usable", Q2_TOUCH_NAME, esp_err_to_name(e));
    ESP_LOGI(TAG, "CO5300 %s %ux%u, QSPI %u MHz / ESP LCD, %s=%s; TE sync disabled", Q2_PANEL_NAME, w, h,
             s->clock_hz / 1000000u, Q2_TOUCH_NAME, s->touch_irq ? "ready" : "unavailable");
    ESP_LOGI(TAG,
             "panel driver=%s: format=%s bpp=%u tile_rows=%u DMA_bytes=%u frame_bytes=%u "
             "queue=%u; software rotation=%u",
             Q2_LCD_DRIVER_NAME, Q2_AM178_PIXEL_NAME, Q2_AM178_PIXEL_BITS, TILE_ROWS, TILE_BYTES, FRAME_BYTES,
             Q2_LCD_QUEUE_DEPTH, s->rotation);
    *out = s;
    return Q2_OK;
}
q2_result_t q2_am178_backend(q2_am178_t *s, q2_pixel_backend_t *out) {
    if (!s || !out)
        return Q2_ERR_ARGUMENT;
    *out = (q2_pixel_backend_t){Q2_PIXEL_ABI, sizeof(*out), s, s->width, s->height, flush};
    return Q2_OK;
}
static q2_result_t touch_read_report(void *context, uint8_t report[Q2_TOUCH_REPORT_BYTES]) {
    q2_am178_t *s = context;
    const uint8_t reg = Q2_TOUCH_REG;
    /* Read the selected controller's complete report with ACK checking. */
    s->touch_last_error = i2c_master_transmit_receive(s->touch, &reg, 1, report, Q2_TOUCH_REPORT_BYTES, 10);
    return result(s->touch_last_error);
}
static q2_result_t touch_capture(q2_am178_t *s, bool irq, bool *pressed, int *x, int *y) {
    uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
    uint32_t reads = s->touch_state.reads, recoveries = s->touch_state.recoveries;
    q2_result_t e = q2_am178_touch_sample(&s->touch_state, s->width, s->height, s->rotation, irq, now,
                                          touch_read_report, s, pressed, x, y);
    if (!s->touch_reported && s->touch_state.reads != reads && e == Q2_OK && !s->touch_state.pending) {
        s->touch_reported = true;
        ESP_LOGD(TAG, "%s first report: contact=%d x=%d y=%d", Q2_TOUCH_NAME, *pressed, *x, *y);
    }
    /* Rate-limit diagnostics without turning persistent failures into successful samples. */
    if (now >= s->touch_log_at_ms && (e != Q2_OK || s->touch_state.recoveries != recoveries)) {
        if (e != Q2_OK)
            ESP_LOGW(TAG, "touch read exhausted %u attempts: %s; INT=%d reads=%lu retries=%lu failed=%lu",
                     s->touch_state.attempts, esp_err_to_name(s->touch_last_error), gpio_get_level(Q2_TP_INT),
                     (unsigned long)s->touch_state.reads, (unsigned long)s->touch_state.retries,
                     (unsigned long)s->touch_state.failures);
        else
            ESP_LOGD(TAG, "touch retry recovered; reads=%lu retries=%lu recovered=%lu failed=%lu",
                     (unsigned long)s->touch_state.reads, (unsigned long)s->touch_state.retries,
                     (unsigned long)s->touch_state.recoveries, (unsigned long)s->touch_state.failures);
        s->touch_log_at_ms = now + 5000;
    }
    return e;
}
/* Only this task accesses the I2C report state. Completed samples cross a FreeRTOS FIFO;
 * the UI task never sees worker-owned mutable state or calls I2C while rendering. */
static void touch_worker(void *context) {
    q2_am178_t *s = context;
    touch_sample_t last = {0};
    uint32_t downs = 0, ups = 0, stalls = 0;
    uint64_t stats_at = 0;
    while (!atomic_load(&s->touch_stop)) {
        TickType_t wait = portMAX_DELAY;
        if (s->touch_state.pending) {
            uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
            uint64_t remaining = s->touch_state.retry_at_ms > now ? s->touch_state.retry_at_ms - now : 0;
            wait = (TickType_t)((remaining + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
        }
        uint8_t pending;
        bool irq = xQueueReceive(s->touch_events, &pending, wait) == pdTRUE;
        if (atomic_load(&s->touch_stop))
            break;
        uint32_t reads = s->touch_state.reads;
        touch_sample_t sample = {0};
        sample.result = touch_capture(s, irq, &sample.pressed, &sample.x, &sample.y);
        /* A cached/retry-wait value is not a new report and must not manufacture an input edge. */
        if (s->touch_state.reads == reads || s->touch_state.pending)
            continue;
        bool edge = sample.pressed != last.pressed;
        bool moved = sample.pressed && (sample.x != last.x || sample.y != last.y);
        if (!edge && !moved && sample.result == Q2_OK)
            continue;
        /* Motion can be superseded, but keep room for press/release transitions. */
        if (!edge && sample.result == Q2_OK && uxQueueSpacesAvailable(s->touch_samples) <= TOUCH_EDGE_RESERVE)
            continue;
        bool queued = xQueueSend(s->touch_samples, &sample, 0) == pdTRUE;
        if (!queued) {
            stalls++;
            ESP_LOGW(TAG, "touch sample FIFO full; acquisition delayed (stalls=%lu)", (unsigned long)stalls);
            /* Retain order instead of overwriting edges. This is bounded by consumer progress;
             * teardown can interrupt it, so an overloaded UI never prevents shutdown. */
            while (!atomic_load(&s->touch_stop) && !queued)
                queued = xQueueSend(s->touch_samples, &sample, 1) == pdTRUE;
        }
        if (!queued)
            break;
        if (edge) {
            if (sample.pressed)
                downs++;
            else
                ups++;
        }
        last = sample;
        uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
        if (now >= stats_at) {
            ESP_LOGD(TAG, "touch capture down=%lu up=%lu queued=%u stalls=%lu", (unsigned long)downs,
                     (unsigned long)ups, (unsigned)uxQueueMessagesWaiting(s->touch_samples),
                     (unsigned long)stalls);
            stats_at = now + 5000;
        }
    }
    /* The owner waits for I2C and queue operations to finish before deleting this task and resources. */
    xSemaphoreGive(s->touch_stopped);
    vTaskSuspend(NULL);
}
q2_result_t q2_am178_touch(q2_am178_t *s, bool *pressed, int *x, int *y) {
    if (!s || !pressed || !x || !y)
        return Q2_ERR_ARGUMENT;
    *pressed = false;
    *x = 0;
    *y = 0;
    if (!s->touch_task || !s->touch_irq)
        return Q2_ERR_UNSUPPORTED;
    touch_sample_t sample;
    if (xQueueReceive(s->touch_samples, &sample, 0) != pdTRUE)
        return Q2_ERR_NOT_FOUND;
    *pressed = sample.pressed;
    *x = sample.x;
    *y = sample.y;
    return sample.result;
}
q2_result_t q2_am178_destroy(q2_am178_t **ptr) {
    if (!ptr || !*ptr)
        return Q2_OK;
    q2_am178_t *s = *ptr;
    if (s->lcd_io) {
        esp_err_t e = esp_lcd_panel_io_del(s->lcd_io);
        if (e != ESP_OK)
            return result(e);
        s->lcd_io = NULL;
    }
    if (s->lcd_panel) {
        esp_err_t e = esp_lcd_panel_del(s->lcd_panel);
        if (e != ESP_OK)
            return result(e);
        s->lcd_panel = NULL;
    }
    /* Detach the producer before releasing its queue or the I2C device. */
    if (s->touch_irq) {
        gpio_intr_disable(Q2_TP_INT);
        gpio_isr_handler_remove(Q2_TP_INT);
        s->touch_irq = false;
    }
    if (s->touch_task) {
        atomic_store(&s->touch_stop, true);
        uint8_t pending = 1;
        xQueueSend(s->touch_events, &pending, 0);
        if (xSemaphoreTake(s->touch_stopped, pdMS_TO_TICKS(1000)) != pdTRUE)
            return Q2_ERR_BUSY;
        vTaskDelete(s->touch_task);
        s->touch_task = NULL;
    }
    if (s->touch_irq_service)
        gpio_uninstall_isr_service();
    if (s->touch_events)
        vQueueDelete(s->touch_events);
    if (s->touch_samples)
        vQueueDelete(s->touch_samples);
    if (s->touch_stopped)
        vSemaphoreDelete(s->touch_stopped);
    if (s->touch)
        i2c_master_bus_rm_device(s->touch);
    if (s->bus)
        i2c_del_master_bus(s->bus);
    if (s->lcd_done)
        vSemaphoreDelete(s->lcd_done);
    if (s->spi_bus)
        spi_bus_free(SPI2_HOST);
    free(s->frame);
    free(s->tile);
    free(s);
    *ptr = NULL;
    return Q2_OK;
}
