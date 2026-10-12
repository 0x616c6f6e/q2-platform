#ifndef Q2_PLATFORM_CONFIG_H
#define Q2_PLATFORM_CONFIG_H
/* Product builds inject a generated header. Independent platform builds retain local defaults.
 * Kernel never includes this file. Edit q2.json in a product workspace.
 */
#ifdef Q2_USE_PROJECT_CONFIG
#include "q2_project_config.h"
#endif
#ifndef Q2_DISPLAY_WIDTH
#define Q2_DISPLAY_WIDTH 502u
#endif
#ifndef Q2_DISPLAY_HEIGHT
#define Q2_DISPLAY_HEIGHT 410u
#endif
#ifndef Q2_KEY_SINGLE_MIN_MS
#define Q2_KEY_SINGLE_MIN_MS 20u
#endif
#ifndef Q2_KEY_DOUBLE_GAP_MS
#define Q2_KEY_DOUBLE_GAP_MS 300u
#endif
#ifndef Q2_KEY_LONG_PRESS_MS
#define Q2_KEY_LONG_PRESS_MS 800u
#endif
#ifndef Q2_DEFAULT_LOG_LEVEL
#define Q2_DEFAULT_LOG_LEVEL 3u
#endif
#ifndef Q2_COMPONENT_AUDIO
#define Q2_COMPONENT_AUDIO 1u
#endif
#ifndef Q2_COMPONENT_NETWORK
#define Q2_COMPONENT_NETWORK 1u
#endif
#ifndef Q2_COMPONENT_DATA
#define Q2_COMPONENT_DATA 1u
#endif
#ifndef Q2_COMPONENT_DISPLAY
#define Q2_COMPONENT_DISPLAY 1u
#endif
#ifndef Q2_COMPONENT_STORAGE
#define Q2_COMPONENT_STORAGE 1u
#endif
#ifndef Q2_COMPONENT_KEYS
#define Q2_COMPONENT_KEYS 1u
#endif
#ifndef Q2_COMPONENT_APPS
#define Q2_COMPONENT_APPS 1u
#endif
#ifndef Q2_COMPONENT_SQLITE
#define Q2_COMPONENT_SQLITE 0u
#endif
#ifndef Q2_WEB_ENABLED
#define Q2_WEB_ENABLED 1u
#endif
/* Standalone backend builds opt in; the product root selects the shipped defaults. */
#ifndef Q2_STATUS_BAR_ENABLED
#define Q2_STATUS_BAR_ENABLED 0u
#endif
#ifndef Q2_STATUS_BAR_HEIGHT
#define Q2_STATUS_BAR_HEIGHT 32u
#endif
#ifndef Q2_SCREENSAVER_ENABLED
#define Q2_SCREENSAVER_ENABLED 0u
#endif
#ifndef Q2_IDLE_TIMEOUT_MS
#define Q2_IDLE_TIMEOUT_MS 30000u
#endif
#ifndef Q2_CLOCK_MOVE_INTERVAL_MS
#define Q2_CLOCK_MOVE_INTERVAL_MS 10000u
#endif
#ifndef Q2_LOCK_ENABLED
#define Q2_LOCK_ENABLED 0u
#endif
#ifndef Q2_STATUS_LEFT
#define Q2_STATUS_LEFT "{time}"
#endif
#ifndef Q2_STATUS_RIGHT
#define Q2_STATUS_RIGHT "{wifi} {bt} {ble} {battery}"
#endif
#ifndef Q2_SAVER_TEXT
#define Q2_SAVER_TEXT "{time}"
#endif
#ifndef Q2_UNLOCK_KEY
#define Q2_UNLOCK_KEY "POWER"
#endif
#ifndef Q2_UNLOCK_ACTION
#define Q2_UNLOCK_ACTION 2u
#endif
#ifndef Q2_DEFAULT_KEY_NAMES
#define Q2_DEFAULT_KEY_NAMES {"POWER", "BACK", "PREV", "NEXT", "PLAY"}
#endif
#if Q2_KEY_SINGLE_MIN_MS == 0 || Q2_KEY_DOUBLE_GAP_MS == 0 || Q2_KEY_LONG_PRESS_MS <= Q2_KEY_SINGLE_MIN_MS
#error "Invalid platform key timing"
#endif
#endif
