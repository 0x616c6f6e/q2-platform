/* Exercise the production pattern renderer and synchronous backend contract. */
#include "am178_area_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned width, height, rows, calls, lit, fail_call;
    q2_am178_area_step_t step;
} capture_t;

static q2_result_t flush(void *context, unsigned x, unsigned y, unsigned width, unsigned height,
                         const uint32_t *pixels, size_t stride) {
    capture_t *c = context;
    assert(x == 0 && y == c->rows && width == c->width);
    assert(height > 0 && height <= 32 && y + height <= c->height);
    assert(stride == width * sizeof(*pixels));
    if (++c->calls == c->fail_call)
        return Q2_ERR_IO;
    for (unsigned row = 0; row < height; ++row) {
        for (unsigned col = 0; col < width; ++col) {
            bool inside = col >= c->step.x && col - c->step.x < c->step.width &&
                y + row >= c->step.y && y + row - c->step.y < c->step.height;
            uint32_t expected = inside ? c->step.rgb : 0;
            assert(pixels[row * width + col] == expected);
            c->lit += expected != 0;
        }
    }
    c->rows += height;
    return Q2_OK;
}

static void check(unsigned width, unsigned height) {
    capture_t c = {.width = width, .height = height};
    q2_pixel_backend_t b = {Q2_PIXEL_ABI, sizeof(b), &c, width, height, flush};
    const unsigned percents[] = {1, 5, 10, 25, 50, 75, 100};
    const unsigned shifts[] = {16, 8, 0};
    const char *names[] = {"RED", "GREEN", "BLUE"};
    q2_am178_area_step_t previous = {0};
    for (unsigned i = 0; i < Q2_AM178_AREA_STEPS; ++i) {
        assert(q2_am178_area_step(i, width, height, &c.step) == Q2_OK);
        assert(c.step.target_percent == percents[i / 6]);
        assert(c.step.rgb == ((i % 2 ? 0xffu : 0xa0u) << shifts[(i / 2) % 3]));
        assert(strcmp(c.step.color_name, names[(i / 2) % 3]) == 0);
        assert(!((c.step.x | c.step.y | c.step.width | c.step.height) & 1));
        assert(c.step.x * 2 + c.step.width <= width);
        assert(width - c.step.x * 2 - c.step.width <= 2);
        assert(c.step.y * 2 + c.step.height <= height);
        assert(height - c.step.y * 2 - c.step.height <= 2);
        assert((uint64_t)c.step.width * c.step.height * 100 <=
               (uint64_t)width * height * c.step.target_percent);
        if (i % 2) {
            assert(c.step.x == previous.x && c.step.y == previous.y);
            assert(c.step.width == previous.width && c.step.height == previous.height);
        }
        if (c.step.target_percent == 100)
            assert(c.step.width == width && c.step.height == height);
        previous = c.step;
        for (unsigned black = 0; black < 2; ++black) {
            c.calls = c.rows = c.lit = 0;
            if (black)
                c.step.rgb = 0;
            assert(q2_am178_area_render(&b, &c.step) == Q2_OK);
            assert(c.rows == height && c.calls == (height + 31) / 32);
            assert(c.lit == (black ? 0 : c.step.width * c.step.height));
        }
    }
    assert(q2_am178_area_step(Q2_AM178_AREA_STEPS, width, height, &c.step) == Q2_ERR_NOT_FOUND);
    c.calls = c.rows = c.lit = 0;
    c.fail_call = 2;
    assert(q2_am178_area_render(&b, &c.step) == Q2_ERR_IO);
    assert(c.calls == 2 && c.rows == 32);
    c.step.width = width + 1;
    assert(q2_am178_area_render(&b, &c.step) == Q2_ERR_ARGUMENT);
    c.step.width = width;
    c.step.rgb = 0x1000000;
    assert(q2_am178_area_render(&b, &c.step) == Q2_ERR_ARGUMENT);
    c.step.rgb = 0;
    b.abi++;
    assert(q2_am178_area_render(&b, &c.step) == Q2_ERR_ARGUMENT);
    assert(q2_am178_area_render(NULL, &c.step) == Q2_ERR_ARGUMENT);
    assert(q2_am178_area_render(&b, NULL) == Q2_ERR_ARGUMENT);
}
int main(void) {
    q2_am178_area_step_t step;
    check(368, 448);
    check(448, 368);
    assert(q2_am178_area_step(0, 63, 448, &step) == Q2_ERR_ARGUMENT);
    assert(q2_am178_area_step(0, 369, 448, &step) == Q2_ERR_ARGUMENT);
    assert(q2_am178_area_step(0, 368, 1026, &step) == Q2_ERR_ARGUMENT);
    assert(q2_am178_area_step(0, 368, 448, NULL) == Q2_ERR_ARGUMENT);
    puts("area patterns: 42 steps x 2 orientations, black/color full frames and failures passed");
    return 0;
}
