#ifndef Q2_EMBEDDED_APPS_H
#define Q2_EMBEDDED_APPS_H
#include "q2/apps.h"
#include "q2/lvgl.h"
/** Immutable flash image; data and descriptor outlive the display provider. */
typedef struct {
    const char *path;
    const unsigned char *data;
    size_t size;
} q2_embedded_image_t;
/** Trusted firmware package, produced by the Q2-App validator/bundler. */
typedef struct {
    const char *id, *icon;
    q2_app_package_t package;
    const q2_embedded_image_t *images;
    size_t image_count;
} q2_embedded_package_t;
typedef struct {
    q2_lvgl_t *display;
    const char *home_id;
    const q2_embedded_package_t *packages;
    size_t count;
} q2_embedded_catalog_t;
/** Register immutable packages and factory roles before Kernel boot. Returns real validation failures. */
q2_result_t q2_embedded_register(q2_embedded_catalog_t *catalog, q2_apps_t *apps);
/** Owner-side prepare callback: load only this APP's assets, preserving resident/other VM images. */
q2_result_t q2_embedded_prepare(void *context, const char *id);
#endif
