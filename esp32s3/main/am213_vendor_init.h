#ifndef Q2_AM213_VENDOR_INIT_H
#define Q2_AM213_VENDOR_INIT_H
/* AM213 table adapted from OSPTEK (CC-BY-4.0), commit 08e5d097dcb5b9768b52f6732ad22435b2f8456e.
 * Source: versions/AM213Q410502LK/examples/esp32s3-idf5_co5300-qspi_esp-lvgl-adapter_lvgl9/
 * main/co5300_init_cmds.h. Only COLMOD/brightness derive from the existing Q2 configuration.
 * Preserve reference boot windows; every draw replaces them with native windows plus X gap 22.
 * The official esp_lcd_co5300 2.0.3 driver owns command/window encoding; AM213 has its own table and CST820
 * touch protocol. */
static const q2_panel_init_cmd_t q2_am178_vendor_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){Q2_AM178_COLMOD}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 60},
    {0x51, (uint8_t[]){Q2_AM178_BRIGHTNESS_REGISTER(Q2_AM178_INITIAL_BRIGHTNESS)}, 1, 0},
    {0x58, (uint8_t[]){0x07}, 1, 10},
};
#endif
