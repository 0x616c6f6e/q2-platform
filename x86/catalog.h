#ifndef Q2_PC_CATALOG_H
#define Q2_PC_CATALOG_H
#include "q2/apps.h"
#include "q2/lvgl.h"
typedef struct {
    const char *manifest_path;
    q2_manifest_t meta;
} q2_pc_package_t;
typedef struct {
    q2_lvgl_t *display;
    const char *home_id;
    q2_pc_package_t packages[Q2_APPS_MAX];
    unsigned count;
} q2_pc_catalog_t;
/** Read a trusted unpacked package, register copied bytes, retain metadata/path for asset preparation. */
q2_result_t q2_pc_catalog_add(q2_pc_catalog_t *catalog, q2_apps_t *apps, const char *manifest,
                              const char *source, const char *const *grants, size_t grant_count,
                              bool factory);
/** APP-manager prepare callback: replace released assets and grant home scoped copies of catalog icons. */
q2_result_t q2_pc_catalog_prepare(void *context, const char *app_id);
#endif
