/* POSIX filesystem regressions; decoding is covered by Kernel LVGL tests. */
#define _POSIX_C_SOURCE 200809L
#include "assets.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct q2_lvgl {
    unsigned width, height, count;
    struct {
        char key[96], owner[37];
        unsigned char marker;
    } images[16];
};
static unsigned checks;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        checks++;                                                                                            \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                                  \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
q2_result_t q2_lvgl_resolution(const q2_lvgl_t *d, unsigned *w, unsigned *h) {
    *w = d->width;
    *h = d->height;
    return Q2_OK;
}
q2_result_t q2_lvgl_register_png(q2_lvgl_t *d, const char *owner, const char *name, const void *data,
                                 size_t size) {
    if (!size)
        return Q2_ERR_FORMAT;
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->images[i].key, name) && !strcmp(d->images[i].owner, owner))
            return Q2_ERR_EXISTS;
    CHECK(d->count < 16);
    strcpy(d->images[d->count].key, name);
    strcpy(d->images[d->count].owner, owner);
    d->images[d->count++].marker = *(const unsigned char *)data;
    return Q2_OK;
}
static unsigned marker(q2_lvgl_t *d, const char *name) {
    for (unsigned i = 0; i < d->count; i++)
        if (!strcmp(d->images[i].key, name))
            return d->images[i].marker;
    return 0;
}
static void write_file(const char *name, const char *data) {
    FILE *f = fopen(name, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(data, 1, strlen(data), f) == strlen(data));
    CHECK(fclose(f) == 0);
}
int main(void) {
    char directory[] = "/tmp/q2-assets-XXXXXX", previous[PATH_MAX];
    CHECK(getcwd(previous, sizeof(previous)) != NULL);
    CHECK(mkdtemp(directory) != NULL);
    CHECK(chdir(directory) == 0);
    CHECK(mkdir("assets", 0700) == 0);
    CHECK(mkdir("assets/502x410", 0700) == 0);
    CHECK(mkdir("assets/410x502", 0700) == 0);
    write_file("assets/icon.png", "A");
    write_file("assets/fallback.png", "B");
    write_file("assets/502x410/icon.png", "V");
    write_file("assets/502x410/extra.png", "E");
    write_file("assets/410x502/icon.png", "P");
    q2_lvgl_t d = {.width = 502, .height = 410};
    const char *owner = "2d7f3e18-b368-43e7-9698-eb29015fc1f8";
    CHECK(q2_pc_assets_load(&d, "manifest.json", owner) == Q2_OK);
    CHECK(d.count == 3 && marker(&d, "assets/icon.png") == 'V');
    CHECK(marker(&d, "assets/fallback.png") == 'B' && marker(&d, "assets/extra.png") == 'E');
    d.count = 0;
    const char *home = "71f147c6-3604-4474-9e3d-af2d949fb38f";
    CHECK(q2_pc_asset_alias(&d, "manifest.json", "assets/icon.png", home, "assets/catalog.png") == Q2_OK);
    CHECK(d.count == 1 && marker(&d, "assets/catalog.png") == 'V');
    CHECK(!strcmp(d.images[0].owner, home));
    d.count = 0;
    d.width = 410;
    d.height = 502;
    CHECK(q2_pc_assets_load(&d, "manifest.json", owner) == Q2_OK);
    CHECK(d.count == 2 && marker(&d, "assets/icon.png") == 'P');
    CHECK(marker(&d, "assets/extra.png") == 0);
    d.count = 0;
    d.width = 320;
    d.height = 240;
    CHECK(q2_pc_assets_load(&d, "manifest.json", owner) == Q2_OK);
    CHECK(d.count == 2 && marker(&d, "assets/icon.png") == 'A');
    CHECK(q2_pc_asset_alias(&d, "manifest.json", "assets/../icon.png", owner, "assets/test.png") ==
          Q2_ERR_ARGUMENT);
    CHECK(q2_pc_asset_alias(&d, "manifest.json", "assets/", owner, "assets/test.png") == Q2_ERR_ARGUMENT);
    CHECK(q2_pc_asset_alias(&d, "manifest.json", "assets/missing.png", owner, "assets/test.png") ==
          Q2_ERR_IO);
    CHECK(q2_pc_assets_load(&d, "absent/manifest.json", owner) == Q2_OK);
    CHECK(q2_pc_assets_load(NULL, "manifest.json", owner) == Q2_ERR_ARGUMENT);
    d.width = 502;
    d.height = 410;
    d.count = 0;
    write_file("assets/502x410/icon.png", "");
    CHECK(q2_pc_asset_alias(&d, "manifest.json", "assets/icon.png", owner, "assets/test.png") ==
          Q2_ERR_FORMAT);
    CHECK(d.count == 0); /* Existing invalid variants must not silently select the fallback. */
    FILE *f = fopen("assets/502x410/icon.png", "wb");
    CHECK(f != NULL && fseek(f, Q2_UI_PNG_MAX, SEEK_SET) == 0 && fputc('X', f) != EOF);
    CHECK(fclose(f) == 0);
    CHECK(q2_pc_asset_alias(&d, "manifest.json", "assets/icon.png", owner, "assets/test.png") ==
          Q2_ERR_LIMIT);
    CHECK(unlink("assets/502x410/icon.png") == 0);
    CHECK(symlink("../icon.png", "assets/502x410/icon.png") == 0);
    CHECK(q2_pc_asset_alias(&d, "manifest.json", "assets/icon.png", owner, "test") == Q2_ERR_IO);
    CHECK(unlink("assets/502x410/icon.png") == 0);
    CHECK(unlink("assets/502x410/extra.png") == 0);
    CHECK(rmdir("assets/502x410") == 0);
    write_file("assets/502x410", "not a directory");
    CHECK(q2_pc_assets_load(&d, "manifest.json", owner) == Q2_ERR_ARGUMENT);
    CHECK(unlink("assets/502x410") == 0);
    CHECK(unlink("assets/410x502/icon.png") == 0);
    CHECK(rmdir("assets/410x502") == 0);
    CHECK(unlink("assets/icon.png") == 0);
    CHECK(unlink("assets/fallback.png") == 0);
    CHECK(rmdir("assets") == 0);
    CHECK(chdir(previous) == 0);
    CHECK(rmdir(directory) == 0);
    printf("PASS: %u native Linux asset checks\n", checks);
    return 0;
}
