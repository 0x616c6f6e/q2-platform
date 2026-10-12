/* Verify production patterns/rendered pixels, including the F8/FF identical-wire-data control. */
#include "am178_blue_test.h"
#include "am178_geometry.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    unsigned width, rows, lit;
    uint32_t hash;
    q2_am178_area_step_t step;
} capture_t;
static q2_result_t capture(void *context, unsigned x, unsigned y, unsigned w, unsigned h,
                           const uint32_t *pixels, size_t stride) {
    capture_t *c = context;
    assert(x == 0 && y == c->rows && w == c->width && stride == w * 4u);
    for (unsigned j = 0; j < h; ++j) {
        for (unsigned i = 0; i < w; ++i) {
            bool lit = i >= c->step.x && i - c->step.x < c->step.width &&
                       y + j >= c->step.y && y + j - c->step.y < c->step.height;
            uint32_t expected = lit ? c->step.rgb : 0;
            assert(pixels[j * w + i] == expected);
            uint16_t wire = q2_am178_rgb565(pixels[j * w + i]);
            c->hash = (c->hash ^ (wire >> 8)) * 16777619u;
            c->hash = (c->hash ^ (wire & 255u)) * 16777619u;
            c->lit += lit;
        }
    }
    c->rows += h;
    return Q2_OK;
}
int main(void) {
    const uint32_t rgb[] = {0xa00000, 0xf00000, 0xf80000, 0xff0000,
                            0x0000f8, 0xa000f8, 0xf000f8, 0xf800f8, 0xff00f8};
    const uint16_t wire[] = {0xa000, 0xf000, 0xf800, 0xf800,
                            0x001f, 0xa01f, 0xf01f, 0xf81f, 0xf81f};
    for (unsigned direction = 0; direction < 2; ++direction) {
        unsigned w = direction ? 448 : 368, h = direction ? 368 : 448;
        uint32_t hashes[Q2_AM178_BLUE_STEPS];
        for (unsigned i = 0; i < Q2_AM178_BLUE_STEPS; ++i) {
            capture_t c = {.width = w, .hash = 2166136261u};
            unsigned hz;
            assert(q2_am178_blue_step(i, w, h, &c.step, &hz) == Q2_OK);
            assert(hz == (i < Q2_AM178_BLUE_LEVELS ? 10000000u : 2000000u));
            assert(c.step.rgb == rgb[i % Q2_AM178_BLUE_LEVELS]);
            assert(q2_am178_rgb565(c.step.rgb) == wire[i % Q2_AM178_BLUE_LEVELS]);
            assert(c.step.width * c.step.height * 100u <= w * h);
            assert((w - c.step.width) / 2u - c.step.x <= 1u);
            assert((h - c.step.height) / 2u - c.step.y <= 1u);
            q2_pixel_backend_t b = {Q2_PIXEL_ABI, sizeof(b), &c, w, h, capture};
            assert(q2_am178_area_render(&b, &c.step) == Q2_OK);
            assert(c.rows == h && c.lit == c.step.width * c.step.height);
            hashes[i] = c.hash;
            if (i >= Q2_AM178_BLUE_LEVELS)
                assert(hashes[i] == hashes[i - Q2_AM178_BLUE_LEVELS]);
        }
        assert(hashes[2] == hashes[3]);
        assert(hashes[7] == hashes[8]);
        assert(hashes[0] != hashes[1]);
        assert(hashes[1] != hashes[2]);
    }
    q2_am178_area_step_t s;
    unsigned hz;
    assert(q2_am178_blue_step(Q2_AM178_BLUE_STEPS, 368, 448, &s, &hz) == Q2_ERR_NOT_FOUND);
    assert(q2_am178_blue_step(0, 63, 448, &s, &hz) == Q2_ERR_ARGUMENT);
    assert(q2_am178_blue_step(0, 368, 448, NULL, &hz) == Q2_ERR_ARGUMENT);
    assert(q2_am178_blue_step(0, 368, 448, &s, NULL) == Q2_ERR_ARGUMENT);
    puts("PASS: blue patterns, both clock groups/orientations, full-frame pixels and F8/FF wire equality");
}
