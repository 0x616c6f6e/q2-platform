#ifndef Q2_PC_ASSETS_H
#define Q2_PC_ASSETS_H
#include "q2/lvgl.h"
/** Import root PNGs and current assets/WIDTHxHEIGHT overrides, bound to the APP UUID.
 * Register only one version per flat logical assets/name.png key; other resolutions stay unloaded.
 * Reject reparse-point asset directories/files; Kernel enforces compressed and pixel quotas. */
q2_result_t q2_pc_assets_load(q2_lvgl_t *display, const char *manifest_path, const char *app_id);
/** Register a logical root PNG under another owner's catalog alias, preferring its resolution variant. */
q2_result_t q2_pc_asset_alias(q2_lvgl_t *display, const char *manifest_path, const char *asset,
                              const char *owner_id, const char *alias);
#endif
