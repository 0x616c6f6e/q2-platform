#include "catalog.h"
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *read_file(const char *path, size_t limit, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *data = malloc(limit + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    *size = fread(data, 1, limit + 1, f);
    bool ok = !ferror(f) && *size <= limit;
    fclose(f);
    if (!ok) {
        free(data);
        return NULL;
    }
    data[*size] = 0;
    return data;
}
q2_result_t q2_pc_catalog_add(q2_pc_catalog_t *c, q2_apps_t *apps, const char *manifest, const char *source,
                              const char *const *grants, size_t count, bool factory) {
    if (!c || !apps || !manifest || !source || !c->home_id)
        return Q2_ERR_ARGUMENT;
    if (c->count == Q2_APPS_MAX)
        return Q2_ERR_LIMIT;
    size_t json_size = 0, code_size = 0;
    char *json = read_file(manifest, Q2_MANIFEST_MAX, &json_size),
         *code = read_file(source, Q2_SOURCE_MAX, &code_size);
    q2_result_t e = json && code ? Q2_OK : Q2_ERR_IO;
    q2_pc_package_t *p = &c->packages[c->count];
    if (e == Q2_OK)
        e = q2_manifest_parse(json, json_size, &p->meta);
    if (e == Q2_OK) {
        q2_app_package_t input = {json,   code,  json_size, code_size,
                                  grants, count, factory,   !strcmp(c->home_id, p->meta.id)};
        e = q2_apps_register(apps, &input);
        if (e == Q2_OK && factory)
            e = q2_lvgl_factory(c->display, p->meta.id);
    }
    free(json);
    free(code);
    if (e == Q2_OK) {
        p->manifest_path = manifest;
        c->count++;
    }
    return e;
}
q2_result_t q2_pc_catalog_prepare(void *context, const char *id) {
    q2_pc_catalog_t *c = context;
    if (!c || !id)
        return Q2_ERR_ARGUMENT;
    q2_result_t e = q2_lvgl_clear_app_images(c->display, id);
    if (e != Q2_OK)
        return e;
    unsigned found = c->count;
    for (unsigned i = 0; i < c->count; i++)
        if (!strcmp(c->packages[i].meta.id, id))
            found = i;
    if (found == c->count)
        return Q2_ERR_NOT_FOUND;
    e = q2_pc_assets_load(c->display, c->packages[found].manifest_path, id);
    if (e != Q2_OK || strcmp(id, c->home_id))
        return e;
    for (unsigned i = 0; i < c->count; i++) {
        q2_pc_package_t *p = &c->packages[i];
        if (!*p->meta.icon || !strcmp(id, p->meta.id))
            continue;
        char alias[96];
        snprintf(alias, sizeof(alias), "assets/catalog-%s.png", p->meta.id);
        /* A bad catalog icon does not brick home; its Lua view substitutes the packaged fallback. */
        (void)q2_pc_asset_alias(c->display, p->manifest_path, p->meta.icon, id, alias);
    }
    return Q2_OK;
}
