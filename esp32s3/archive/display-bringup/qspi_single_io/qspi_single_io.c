#include "qspi_single_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    esp_lcd_panel_io_t base; /* First member supports conversion from public handle. */
    spi_device_handle_t device;
    size_t max_bytes;
    esp_lcd_panel_io_color_trans_done_cb_t callback;
    void *context;
} single_io_t;

static void IRAM_ATTR transfer_done(spi_transaction_t *transaction) {
    single_io_t *io = transaction->user;
    /* Parameter writes never notify. Color completion runs in SPI ISR, not caller task. */
    if (io && io->callback && io->callback(&io->base, NULL, io->context))
        portYIELD_FROM_ISR();
}
static esp_err_t transmit(single_io_t *io, int command, const void *data, size_t bytes, bool color) {
    uint32_t encoded = (uint32_t)command;
    uint32_t opcode = encoded >> 24;
    if ((bytes && !data) || bytes > io->max_bytes || bytes > SIZE_MAX / 8 ||
        (encoded & 0x00ff00ffu) != 0 || opcode != (color ? 0x32u : 0x02u) || (color && !bytes))
        return ESP_ERR_INVALID_ARG;
    /* Header and QIO pixels belong to one hardware transaction. Command/address
     * remain single-line: do not set MULTILINE_CMD or MULTILINE_ADDR. */
    spi_transaction_t transaction = {
        .cmd = (uint16_t)opcode, .addr = encoded & 0x00ffffffu,
        .length = bytes * 8, .tx_buffer = bytes ? data : NULL,
        .flags = color ? SPI_TRANS_MODE_QIO : 0, .user = color ? io : NULL,
    };
    /* Synchronous collection also checks SDK DMA error flags. This still uses DMA
     * when enabled on the bus; synchronous is not a no-DMA mode. */
    return spi_device_transmit(io->device, &transaction);
}
static esp_err_t write_param(esp_lcd_panel_io_t *base, int cmd, const void *data, size_t bytes) {
    return transmit((single_io_t *)base, cmd, data, bytes, false);
}
static esp_err_t write_color(esp_lcd_panel_io_t *base, int cmd, const void *data, size_t bytes) {
    return transmit((single_io_t *)base, cmd, data, bytes, true);
}
static esp_err_t read_param(esp_lcd_panel_io_t *base, int cmd, void *data, size_t bytes) {
    (void)base; (void)cmd; (void)data; (void)bytes;
    return ESP_ERR_NOT_SUPPORTED;
}
static esp_err_t callbacks(esp_lcd_panel_io_t *base, const esp_lcd_panel_io_callbacks_t *cb, void *ctx) {
    if (!cb)
        return ESP_ERR_INVALID_ARG;
    single_io_t *io = (single_io_t *)base;
    /* Single-owner synchronous calls guarantee no outstanding callback here. */
    io->callback = cb->on_color_trans_done;
    io->context = ctx;
    return ESP_OK;
}
static esp_err_t destroy(esp_lcd_panel_io_t *base) {
    single_io_t *io = (single_io_t *)base;
    esp_err_t result = spi_bus_remove_device(io->device);
    if (result == ESP_OK)
        free(io);
    return result;
}
esp_err_t esp_lcd_new_panel_io_qspi_single(esp_lcd_spi_bus_handle_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || !bus || config->dc_gpio_num != -1 || config->lcd_cmd_bits != 32 ||
        config->lcd_param_bits != 8 || !config->flags.quad_mode || config->flags.octal_mode ||
        config->flags.sio_mode || config->flags.lsb_first || config->flags.cs_high_active ||
        config->flags.psram_dma_direct || !config->pclk_hz)
        return ESP_ERR_INVALID_ARG;
    single_io_t *io = heap_caps_calloc(1, sizeof(*io), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!io)
        return ESP_ERR_NO_MEM;
    spi_device_interface_config_t device = {
        .command_bits = 8, .address_bits = 24, .dummy_bits = 0,
        .mode = config->spi_mode, .clock_speed_hz = config->pclk_hz,
        .spics_io_num = config->cs_gpio_num, .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .cs_ena_pretrans = config->cs_ena_pretrans, .cs_ena_posttrans = config->cs_ena_posttrans,
        .post_cb = transfer_done,
    };
    /* ESP-IDF creates the SPI master context lazily when the first device is added.
     * Querying transfer limits before this returns INVALID_ARG on a fresh bus. */
    esp_err_t result = spi_bus_add_device((spi_host_device_t)bus, &device, &io->device);
    if (result != ESP_OK) {
        free(io);
        return result;
    }
    result = spi_bus_get_max_transaction_len((spi_host_device_t)bus, &io->max_bytes);
    if (result != ESP_OK) {
        /* No transfers have been submitted; release the newly registered device. */
        spi_bus_remove_device(io->device);
        free(io);
        return result;
    }
    io->callback = config->on_color_trans_done;
    io->context = config->user_ctx;
    io->base = (esp_lcd_panel_io_t){.tx_param = write_param, .tx_color = write_color,
        .rx_param = read_param, .del = destroy, .register_event_callbacks = callbacks};
    *out = &io->base;
    return ESP_OK;
}
