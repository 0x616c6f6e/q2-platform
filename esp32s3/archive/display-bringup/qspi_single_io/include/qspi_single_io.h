#pragma once
#include "esp_lcd_io_spi.h"
/** Create a CO5300 single-hardware-transaction QSPI IO on an initialized bus.
 * Owner-task calls only. Encoded 32-bit commands become 8 hardware command bits
 * plus 24 address bits, followed by single-line parameters or four-line pixels.
 * Writes block through final SPI result collection. Buffers remain owned by caller.
 * Optional color callback runs in SPI ISR; true requests an ISR yield.
 * Rejects incompatible config, raw continuation, oversized writes and readback.
 * Returns ESP_OK or allocation/SPI errors; out is NULL on failure.
 * Release with esp_lcd_panel_io_del after owner operations finish.
 */
esp_err_t esp_lcd_new_panel_io_qspi_single(esp_lcd_spi_bus_handle_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *out);
