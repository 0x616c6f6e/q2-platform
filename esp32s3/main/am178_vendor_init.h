#ifndef Q2_AM178_VENDOR_INIT_H
#define Q2_AM178_VENDOR_INIT_H
/* Initialization list copied from the user's OSPTEK AM178 lcd_init.c (2026 OSPTEK).
 * Board changes: keep the user-selected 0x63=0x80; derive 0x51 from the single brightness setting.
 * 0x3A follows the selected diagnostic pixel format; all other payloads/delays retain the reference.
 * See ../docs/am178-vendor-alignment.md for attribution.
 * Keep the reference boot ranges; draw_bitmap sets the native transfer windows. */
#include "am178_config.h"
#include "panel_profile.h"
#if Q2_PANEL_AM213
#include "esp_lcd_co5300.h"
typedef co5300_lcd_init_cmd_t q2_panel_init_cmd_t;
#include "am213_vendor_init.h"
#else
#include "esp_lcd_qspi_amoled.h"
typedef qspi_amoled_lcd_init_cmd_t q2_panel_init_cmd_t;
static const q2_panel_init_cmd_t q2_am178_vendor_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){Q2_AM178_COLMOD}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x63, (uint8_t[]){0x80}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 60},
    {0x51, (uint8_t[]){Q2_AM178_BRIGHTNESS_REGISTER(Q2_AM178_INITIAL_BRIGHTNESS)}, 1, 0},
    {0x58, (uint8_t[]){0x07}, 1, 10},
};
#endif /* panel selection */
#endif
