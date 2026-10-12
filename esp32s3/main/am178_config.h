#ifndef Q2_AM178_CONFIG_H
#define Q2_AM178_CONFIG_H
/* Product builds have one settings source: generated values from q2.esp32s3.json.
 * Kconfig supplies defaults only to independent Platform builds. */
#include "panel_profile.h"
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
#define Q2_AM178_PIXEL_BITS 16u
#define Q2_AM178_PIXEL_BYTES 2u
#define Q2_AM178_COLMOD 0x55u
#define Q2_AM178_PIXEL_NAME "RGB565"
/* Percent -> 8-bit register mapping, shared by initialization and runtime control.
 * Caller validates 0..100 before use; this macro accepts a side-effect-free expression. */
#if Q2_PANEL_AM213
/* Official CO5300 API accepts a percentage and truncates the register conversion. */
#define Q2_AM178_BRIGHTNESS_REGISTER(percent) (((percent) * 255u) / 100u)
#else
#define Q2_AM178_BRIGHTNESS_REGISTER(percent) (((percent) * 255u + 50u) / 100u)
#endif
#endif
