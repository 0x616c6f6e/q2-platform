#include "embedded_apps.h"
#include <stdio.h>
#include <string.h>

q2_result_t q2_embedded_register(q2_embedded_catalog_t *c, q2_apps_t *apps) {
    if (!c || !apps || !c->display || !c->home_id || !c->packages || !c->count)
        return Q2_ERR_ARGUMENT;
    for (size_t i = 0; i < c->count; i++) {
        q2_result_t e = q2_apps_register(apps, &c->packages[i].package);
        if (e == Q2_OK && c->packages[i].package.factory)
            e = q2_lvgl_factory(c->display, c->packages[i].id);
        if (e != Q2_OK)
            return e;
    }
    return Q2_OK;
}
q2_result_t q2_embedded_prepare(void *context, const char *id) {
    q2_embedded_catalog_t *c = context;
    if (!c || !id)
        return Q2_ERR_ARGUMENT;
    const q2_embedded_package_t *p = NULL;
    for (size_t i = 0; i < c->count; i++)
        if (!strcmp(id, c->packages[i].id))
            p = &c->packages[i];
    if (!p)
        return Q2_ERR_NOT_FOUND;
    q2_result_t e = q2_lvgl_clear_app_images(c->display, id);
    for (size_t i = 0; i < p->image_count && e == Q2_OK; i++) {
        const q2_embedded_image_t *image = &p->images[i];
        e = q2_lvgl_register_png(c->display, id, image->path, image->data, image->size);
    }
    if (e != Q2_OK || strcmp(id, c->home_id))
        return e;
    /* Home owns catalog aliases so opening Settings cannot invalidate the resident launcher. */
    for (size_t i = 0; i < c->count; i++) {
        p = &c->packages[i];
        if (!strcmp(id, p->id))
            continue;
        for (size_t j = 0; j < p->image_count; j++) {
            const q2_embedded_image_t *image = &p->images[j];
            if (strcmp(image->path, p->icon))
                continue;
            char alias[96];
            snprintf(alias, sizeof(alias), "assets/catalog-%s.png", p->id);
            e = q2_lvgl_register_png(c->display, id, alias, image->data, image->size);
            if (e != Q2_OK)
                return e;
        }
    }
    return Q2_OK;
}
