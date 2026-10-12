#define _POSIX_C_SOURCE 200809L
#include "assets.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static q2_result_t asset_path(char *out, const char *folder, const char *name) {
    int count = snprintf(out, PATH_MAX, "%s/%s", folder, name);
    return count < 0 || count >= PATH_MAX ? Q2_ERR_LIMIT : Q2_OK;
}
static q2_result_t folders(q2_lvgl_t *display, const char *manifest, char *base, char *variant) {
    const char *end = strrchr(manifest, '/');
    size_t prefix = end ? (size_t)(end + 1 - manifest) : 0;
    if (prefix + 7 >= PATH_MAX)
        return Q2_ERR_LIMIT;
    memcpy(base, manifest, prefix);
    strcpy(base + prefix, "assets");
    unsigned width, height;
    q2_result_t e = q2_lvgl_resolution(display, &width, &height);
    if (e != Q2_OK)
        return e;
    int n = snprintf(variant, PATH_MAX, "%s/%ux%u", base, width, height);
    if (n < 0 || n >= PATH_MAX)
        return Q2_ERR_LIMIT;
    for (unsigned i = 0; i < 2; i++) {
        struct stat info;
        if (lstat(i ? variant : base, &info) != 0) {
            if (errno == ENOENT)
                continue;
            return Q2_ERR_IO;
        }
        if (!S_ISDIR(info.st_mode))
            return Q2_ERR_ARGUMENT;
    }
    return Q2_OK;
}
q2_result_t q2_pc_asset_alias(q2_lvgl_t *display, const char *manifest, const char *asset, const char *owner,
                              const char *alias) {
    if (!display || !manifest || !asset || !owner || !alias || strncmp(asset, "assets/", 7) || !asset[7] ||
        strchr(asset + 7, '/') || strchr(asset + 7, '\\') || strstr(asset, ".."))
        return Q2_ERR_ARGUMENT;
    char base[PATH_MAX], variant[PATH_MAX], path[PATH_MAX];
    q2_result_t e = folders(display, manifest, base, variant);
    if (e != Q2_OK)
        return e;
    e = asset_path(path, variant, asset + 7);
    if (e != Q2_OK)
        return e;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 && errno == ENOENT) {
        e = asset_path(path, base, asset + 7);
        if (e != Q2_OK)
            return e;
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    }
    if (fd < 0)
        return Q2_ERR_IO;
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        close(fd);
        return Q2_ERR_IO;
    }
    if (info.st_size < 0 || info.st_size > Q2_UI_PNG_MAX) {
        close(fd);
        return Q2_ERR_LIMIT;
    }
    unsigned char *data = malloc(Q2_UI_PNG_MAX + 1);
    if (!data) {
        close(fd);
        return Q2_ERR_MEMORY;
    }
    size_t used = 0;
    while (used <= Q2_UI_PNG_MAX) {
        ssize_t count = read(fd, data + used, Q2_UI_PNG_MAX + 1 - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            e = Q2_ERR_IO;
            break;
        }
        if (count == 0)
            break;
        used += (size_t)count;
    }
    close(fd);
    if (e == Q2_OK)
        e = used > Q2_UI_PNG_MAX ? Q2_ERR_LIMIT : q2_lvgl_register_png(display, owner, alias, data, used);
    free(data);
    return e;
}
static q2_result_t load_folder(q2_lvgl_t *display, const char *manifest, const char *owner,
                               const char *folder, const char *base, bool variant_only) {
    DIR *dir = opendir(folder);
    if (!dir)
        return errno == ENOENT ? Q2_OK : Q2_ERR_IO;
    q2_result_t e = Q2_OK;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno)
                e = Q2_ERR_IO;
            break;
        }
        size_t length = strlen(entry->d_name);
        if (length < 4 || strcmp(entry->d_name + length - 4, ".png"))
            continue;
        struct stat info;
        if (fstatat(dirfd(dir), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(info.st_mode)) {
            e = Q2_ERR_ARGUMENT;
            break;
        }
        /* Base aliases already selected their resolution variant; don't register them twice. */
        if (variant_only) {
            char original[PATH_MAX];
            e = asset_path(original, base, entry->d_name);
            if (e != Q2_OK)
                break;
            if (lstat(original, &info) == 0)
                continue;
            if (errno != ENOENT) {
                e = Q2_ERR_IO;
                break;
            }
        }
        char key[96];
        int count = snprintf(key, sizeof(key), "assets/%s", entry->d_name);
        if (count < 0 || (size_t)count >= sizeof(key)) {
            e = Q2_ERR_LIMIT;
            break;
        }
        e = q2_pc_asset_alias(display, manifest, key, owner, key);
        if (e != Q2_OK)
            break;
    }
    closedir(dir);
    return e;
}
q2_result_t q2_pc_assets_load(q2_lvgl_t *display, const char *manifest, const char *owner) {
    if (!display || !manifest || !owner)
        return Q2_ERR_ARGUMENT;
    char base[PATH_MAX], variant[PATH_MAX];
    q2_result_t e = folders(display, manifest, base, variant);
    if (e == Q2_OK)
        e = load_folder(display, manifest, owner, base, base, false);
    if (e == Q2_OK)
        e = load_folder(display, manifest, owner, variant, base, true);
    return e;
}
