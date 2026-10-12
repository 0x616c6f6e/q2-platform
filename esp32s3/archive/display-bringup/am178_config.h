#ifndef Q2_AM178_CONFIG_H
#define Q2_AM178_CONFIG_H
/* Product builds have one settings source: generated values from q2.esp32s3.json.
 * Kconfig supplies defaults only to independent Platform builds. */
#include "sdkconfig.h"
#ifdef Q2_USE_PROJECT_CONFIG
#include "q2_project_config.h"
#define Q2_AM178_INITIAL_BRIGHTNESS Q2_DISPLAY_BRIGHTNESS
#else
#define Q2_AM178_INITIAL_BRIGHTNESS CONFIG_Q2_AM178_INITIAL_BRIGHTNESS
#endif
#if Q2_AM178_INITIAL_BRIGHTNESS < 0 || Q2_AM178_INITIAL_BRIGHTNESS > 100
#error "AM178 initial brightness must be an integer percentage in 0..100"
#endif
/* One pixel-format selection drives COLMOD, vendor bit depth and every DMA byte count. */
#if defined(CONFIG_Q2_AM178_BLUE_RGB888) && CONFIG_Q2_AM178_BLUE_RGB888
#define Q2_AM178_PIXEL_BITS 24u
#define Q2_AM178_PIXEL_BYTES 3u
#define Q2_AM178_COLMOD 0x77u
#define Q2_AM178_PIXEL_NAME "RGB888"
#else
#define Q2_AM178_PIXEL_BITS 16u
#define Q2_AM178_PIXEL_BYTES 2u
#define Q2_AM178_COLMOD 0x55u
#define Q2_AM178_PIXEL_NAME "RGB565"
#endif
/* Rounded percent -> 8-bit register mapping, shared by initialization and runtime control.
 * Caller validates 0..100 before use; this macro accepts a side-effect-free expression. */
#define Q2_AM178_BRIGHTNESS_REGISTER(percent) (((percent) * 255u + 50u) / 100u)
#endif
