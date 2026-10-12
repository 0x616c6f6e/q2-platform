#ifndef Q2_PANEL_PROFILE_H
#define Q2_PANEL_PROFILE_H
/* Geometry and touch protocol are selected together. Product JSON owns selection;
 * Kconfig is only the fallback for standalone ESP builds. Host tests may define Q2_PANEL_AM213. */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifdef Q2_USE_PROJECT_CONFIG
#include "q2_project_config.h"
#endif
#ifndef Q2_PANEL_AM213
#ifdef Q2_USE_PROJECT_CONFIG
#define Q2_PANEL_AM213 Q2_DISPLAY_PANEL_AM213
#elif defined(CONFIG_Q2_PANEL_AM213) && CONFIG_Q2_PANEL_AM213
#define Q2_PANEL_AM213 1
#else
#define Q2_PANEL_AM213 0
#endif
#endif
#if Q2_PANEL_AM213
#define Q2_PANEL_NAME "AM213"
#define Q2_PANEL_NATIVE_WIDTH 410u
#define Q2_PANEL_NATIVE_HEIGHT 502u
#define Q2_PANEL_X_GAP 22u
#define Q2_TOUCH_NAME "CST820"
#define Q2_TOUCH_ADDRESS 0x15u
#define Q2_TOUCH_REG 0x00u
#define Q2_TOUCH_REPORT_BYTES 15u
#else
#define Q2_PANEL_NAME "AM178"
#define Q2_PANEL_NATIVE_WIDTH 368u
#define Q2_PANEL_NATIVE_HEIGHT 448u
#define Q2_PANEL_X_GAP 16u
#define Q2_TOUCH_NAME "CHSC6417"
#define Q2_TOUCH_ADDRESS 0x2eu
#define Q2_TOUCH_REG 0xe0u
#define Q2_TOUCH_REPORT_BYTES 3u
#endif
#endif
