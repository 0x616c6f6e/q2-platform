#include "am178_blue_test.h"
#include <stdlib.h>
q2_result_t q2_am178_blue_step(unsigned index, unsigned width, unsigned height,
                               q2_am178_area_step_t *step, unsigned *clock_hz) {
    if (!step || !clock_hz)
        return Q2_ERR_ARGUMENT;
    if (index >= Q2_AM178_BLUE_STEPS)
        return Q2_ERR_NOT_FOUND;
    q2_result_t e = q2_am178_area_step(0, width, height, step);
    if (e != Q2_OK)
        return e;
    /* Preserve the user's red and mixed colors; reject a mismatched declared count. */
    static const uint32_t colors[] = {
        0xa00000, 0xf00000, 0xf80000, 0xff0000,
        0x0000f8, 0xa000f8, 0xf000f8, 0xf800f8, 0xff00f8,
    };
    _Static_assert(sizeof(colors) / sizeof(colors[0]) == Q2_AM178_BLUE_LEVELS,
                   "Update Q2_AM178_BLUE_LEVELS when changing the palette");
    step->rgb = colors[index % Q2_AM178_BLUE_LEVELS];
    step->color_name = "PATTERN";
    *clock_hz = index < Q2_AM178_BLUE_LEVELS ? 10000000u : 2000000u;
    return Q2_OK;
}

q2_result_t q2_am178_blue_patch(const q2_pixel_backend_t *b, const q2_am178_area_step_t *s) {
    if (!b || !s || b->abi != Q2_PIXEL_ABI || b->size < sizeof(*b) || !b->flush ||
        b->width < 64 || b->height < 64 || b->width > 1024 || b->height > 1024 ||
        s->rgb > 0xffffffu || !s->width || !s->height || s->x >= b->width || s->y >= b->height ||
        s->width > b->width - s->x || s->height > b->height - s->y)
        return Q2_ERR_ARGUMENT;
    unsigned band_rows = s->height < 32u ? s->height : 32u;
    size_t count = (size_t)s->width * band_rows;
    uint32_t *pixels = malloc(count * sizeof(*pixels));
    if (!pixels)
        return Q2_ERR_MEMORY;
    for (size_t i = 0; i < count; ++i)
        pixels[i] = s->rgb;
    q2_result_t result = Q2_OK;
    /* Only this rectangle is transmitted. The reference background stays in panel RAM. */
    for (unsigned y = 0; y < s->height && result == Q2_OK; y += band_rows) {
        unsigned rows = s->height - y;
        if (rows > band_rows)
            rows = band_rows;
        result = b->flush(b->context, s->x, s->y + y, s->width, rows, pixels,
                          s->width * sizeof(*pixels));
    }
    free(pixels);
    return result;
}
q2_result_t q2_am178_blue_baseline(const q2_pixel_backend_t *b) {
    if (!b)
        return Q2_ERR_ARGUMENT;
    q2_am178_area_step_t center;
    q2_result_t e = q2_am178_area_step(0, b->width, b->height, &center);
    if (e != Q2_OK)
        return e;
    /* Fill the configured reference without a center overlay. */
    q2_am178_area_step_t background = center;
    background.x = 0;
    background.y = 0;
    background.width = b->width;
    background.height = b->height;
    background.rgb = Q2_AM178_BLUE_BACKGROUND;
    /* Background phase has no center overlay; the first blue write belongs to BLUE_HOLD. */
    return q2_am178_area_render(b, &background);
}
