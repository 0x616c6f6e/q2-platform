#ifndef Q2_S3_BOARD_H
#define Q2_S3_BOARD_H
/* User-supplied wiring: GPIO3/46 are strapping inputs at reset. GPIO46 is input-only.
 * GPIO17/18 are display signals; never reuse the copied S31 UART GPIO58/59 settings. */
#define Q2_TP_SCL 9
#define Q2_TP_SDA 46
#define Q2_TP_RST 3
#define Q2_TP_INT 8
#define Q2_LCD_D3 10
#define Q2_LCD_D2 11
#define Q2_LCD_RST 18
#define Q2_LCD_TE 17
#define Q2_LCD_CS 16
#define Q2_LCD_CLK 15
#define Q2_LCD_D1 7
#define Q2_LCD_D0 6

#include "panel_profile.h"
#if Q2_PANEL_AM213
/* OSPTEK AM213 LVGL9 reference: SPI mode 0, 40 MHz, one queued transfer, 80-row bus capacity. */
#define Q2_LCD_CLOCK_HZ 40000000
#define Q2_LCD_TILE_ROWS 80u
#define Q2_LCD_QUEUE_DEPTH 1u
#define Q2_LCD_DRIVER_NAME "esp_lcd_co5300/2.0.3"
#else
/* Preserve the AM178 bring-up transport. */
#define Q2_LCD_CLOCK_HZ 10000000
#define Q2_LCD_TILE_ROWS 10u
#define Q2_LCD_QUEUE_DEPTH 10u
#define Q2_LCD_DRIVER_NAME "osptek_qspi_amoled"
#endif
#endif
