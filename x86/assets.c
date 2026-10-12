#define WIN32_LEAN_AND_MEAN
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static bool missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
/* Build trusted package paths once; never follow an asset directory junction. */
static q2_result_t folders(q2_lvgl_t *display, const char *manifest, char *base, char *variant) {
    const char *end = manifest;
    for (const char *p = manifest; *p; p++)
        if (*p == '/' || *p == '\\')
            end = p + 1;
    size_t prefix = (size_t)(end - manifest);
    if (prefix + 7 >= MAX_PATH)
        return Q2_ERR_LIMIT;
    memcpy(base, manifest, prefix);
    strcpy(base + prefix, "assets");
    unsigned width, height;
    q2_result_t e = q2_lvgl_resolution(display, &width, &height);
    if (e != Q2_OK)
        return e;
    int count = snprintf(variant, MAX_PATH, "%s/%ux%u", base, width, height);
    if (count < 0 || count >= MAX_PATH)
        return Q2_ERR_LIMIT;
    for (unsigned i = 0; i < 2; i++) {
        DWORD attr = GetFileAttributesA(i ? variant : base);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            if (missing(GetLastError()))
                continue;
            return Q2_ERR_IO;
        }
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY) || attr & FILE_ATTRIBUTE_REPARSE_POINT)
            return Q2_ERR_ARGUMENT;
    }
    return Q2_OK;
}
static q2_result_t asset_path(char *out, const char *folder, const char *name) {
    int count = snprintf(out, MAX_PATH, "%s/%s", folder, name);
    return count < 0 || count >= MAX_PATH ? Q2_ERR_LIMIT : Q2_OK;
}
q2_result_t q2_pc_asset_alias(q2_lvgl_t *display, const char *manifest, const char *asset, const char *owner,
                              const char *alias) {
    if (!display || !manifest || !asset || !owner || !alias || strncmp(asset, "assets/", 7) ||
        !asset[7] || strchr(asset + 7, '/') || strchr(asset + 7, '\\') || strstr(asset, ".."))
        return Q2_ERR_ARGUMENT;
    char base[MAX_PATH], variant[MAX_PATH], path[MAX_PATH];
    q2_result_t e = folders(display, manifest, base, variant);
    if (e != Q2_OK)
        return e;
    /* Variant names stay private to the loader. Lua uses a stable assets/name.png key. */
    e = asset_path(path, variant, asset + 7);
    if (e != Q2_OK)
        return e;
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!missing(GetLastError()))
            return Q2_ERR_IO;
        e = asset_path(path, base, asset + 7);
        if (e != Q2_OK)
            return e;
        attr = GetFileAttributesA(path);
    }
    if (attr == INVALID_FILE_ATTRIBUTES || attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return Q2_ERR_IO;
    FILE *f = fopen(path, "rb");
    if (!f)
        return Q2_ERR_IO;
    unsigned char *data = malloc(Q2_UI_PNG_MAX + 1);
    if (!data) {
        fclose(f);
        return Q2_ERR_MEMORY;
    }
    size_t size = fread(data, 1, Q2_UI_PNG_MAX + 1, f);
    bool valid = !ferror(f) && size <= Q2_UI_PNG_MAX;
    fclose(f);
    e = valid ? q2_lvgl_register_png(display, owner, alias, data, size) : Q2_ERR_LIMIT;
    free(data);
    return e;
}
static q2_result_t load_folder(q2_lvgl_t *display, const char *manifest, const char *owner,
                                const char *folder, const char *base, bool variant_only) {
    char pattern[MAX_PATH];
    q2_result_t result = asset_path(pattern, folder, "*.png");
    if (result != Q2_OK)
        return result;
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return missing(GetLastError()) ? Q2_OK : Q2_ERR_IO;
    do {
        if (found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
            result = Q2_ERR_ARGUMENT;
            break;
        }
        /* Root names have already been loaded with variant priority; add only variant-exclusive names. */
        if (variant_only) {
            char original[MAX_PATH];
            result = asset_path(original, base, found.cFileName);
            if (result != Q2_OK)
                break;
            DWORD attr = GetFileAttributesA(original);
            if (attr != INVALID_FILE_ATTRIBUTES)
                continue;
            if (!missing(GetLastError())) {
                result = Q2_ERR_IO;
                break;
            }
        }
        char key[96];
        int count = snprintf(key, sizeof(key), "assets/%s", found.cFileName);
        if (count < 0 || (size_t)count >= sizeof(key)) {
            result = Q2_ERR_LIMIT;
            break;
        }
        result = q2_pc_asset_alias(display, manifest, key, owner, key);
        if (result != Q2_OK)
            break;
    } while (FindNextFileA(search, &found));
    if (result == Q2_OK && GetLastError() != ERROR_NO_MORE_FILES)
        result = Q2_ERR_IO;
    FindClose(search);
    return result;
}
q2_result_t q2_pc_assets_load(q2_lvgl_t *display, const char *manifest_path, const char *app_id) {
    if (!display || !manifest_path || !app_id)
        return Q2_ERR_ARGUMENT;
    char base[MAX_PATH], variant[MAX_PATH];
    q2_result_t e = folders(display, manifest_path, base, variant);
    if (e != Q2_OK)
        return e;
    e = load_folder(display, manifest_path, app_id, base, base, false);
    if (e == Q2_OK)
        e = load_folder(display, manifest_path, app_id, variant, base, true);
    return e;
}
