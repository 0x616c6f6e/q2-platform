/* Portable diagnostic pattern: constant full-frame traffic, variable illuminated area. */
#include "am178_area_test.h"
#include <stdlib.h>

static unsigned extent(unsigned pixels, unsigned percent) {
    unsigned size = 2;
    /* Integer square root of the requested area ratio, rounded down to an even dimension. */
    for (unsigned next = 4; next <= pixels; next += 2) {
        if ((uint64_t)next * next * 100u > (uint64_t)pixels * pixels * percent)
            break;
        size = next;
    }
    return size;
}
q2_result_t q2_am178_area_step(unsigned index, unsigned width, unsigned height,
                               q2_am178_area_step_t *out) {
    if (!out || width < 64 || height < 64 || width > 1024 || height > 1024 ||
        (width & 1u) || (height & 1u))
        return Q2_ERR_ARGUMENT;
    if (index >= Q2_AM178_AREA_STEPS)
        return Q2_ERR_NOT_FOUND;
    static const unsigned percents[] = {1, 5, 10, 25, 50, 75, 100};
    static const unsigned shifts[] = {16, 8, 0};
    static const char *names[] = {"RED", "GREEN", "BLUE"};
    unsigned color = (index / 2u) % 3u;
    unsigned percent = percents[index / 6u];
    unsigned w = extent(width, percent), h = extent(height, percent);
    *out = (q2_am178_area_step_t){
        .x = ((width - w) / 2u) & ~1u, .y = ((height - h) / 2u) & ~1u,
        .width = w, .height = h, .target_percent = percent,
        .rgb = (index % 2u ? 0xffu : 0xa0u) << shifts[color], .color_name = names[color]};
    return Q2_OK;
}
q2_result_t q2_am178_area_render(const q2_pixel_backend_t *b, const q2_am178_area_step_t *s) {
    if (!b || !s || b->abi != Q2_PIXEL_ABI || b->size < sizeof(*b) || !b->flush ||
        b->width < 64 || b->height < 64 || b->width > 1024 || b->height > 1024 ||
        s->rgb > 0xffffffu || !s->width || !s->height || s->x >= b->width || s->y >= b->height ||
        s->width > b->width - s->x || s->height > b->height - s->y)
        return Q2_ERR_ARGUMENT;
    /* Match the existing LVGL chunk height; every pattern sends the same full pixel count. */
    uint32_t *pixels = malloc((size_t)b->width * 32u * sizeof(*pixels));
    if (!pixels)
        return Q2_ERR_MEMORY;
    q2_result_t result = Q2_OK;
    for (unsigned y = 0; y < b->height && result == Q2_OK; y += 32u) {
        unsigned rows = b->height - y;
        if (rows > 32u)
            rows = 32u;
        for (unsigned row = 0; row < rows; ++row)
            for (unsigned x = 0; x < b->width; ++x)
                pixels[row * b->width + x] = x >= s->x && x - s->x < s->width &&
                    y + row >= s->y && y + row - s->y < s->height ? s->rgb : 0;
        result = b->flush(b->context, 0, y, b->width, rows, pixels, b->width * sizeof(*pixels));
    }
    free(pixels);
    return result;
}
