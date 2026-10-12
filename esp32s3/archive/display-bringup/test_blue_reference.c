#include "am178_blue_test.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {
    unsigned width, height, calls, fail_at;
    uint32_t *pixels;
    bool only_center;
    q2_am178_area_step_t center;
} screen_t;
static q2_result_t capture(void *ctx, unsigned x, unsigned y, unsigned w, unsigned h,
                           const uint32_t *pixels, size_t stride) {
    screen_t *s = ctx;
    s->calls++;
    if (s->fail_at && s->calls == s->fail_at)
        return Q2_ERR_IO;
    assert(x + w <= s->width && y + h <= s->height && stride == w * 4u);
    if (s->only_center) {
        assert(x == s->center.x && w == s->center.width);
        assert(y >= s->center.y && y + h <= s->center.y + s->center.height);
    }
    for (unsigned j = 0; j < h; ++j)
        for (unsigned i = 0; i < w; ++i)
            s->pixels[(y + j) * s->width + x + i] = pixels[j * w + i];
    return Q2_OK;
}
static void verify(const screen_t *s, uint32_t center_rgb) {
    for (unsigned y = 0; y < s->height; ++y) {
        for (unsigned x = 0; x < s->width; ++x) {
            bool center = y >= s->center.y && y < s->center.y + s->center.height &&
                          x >= s->center.x && x < s->center.x + s->center.width;
            uint32_t expected = center ? center_rgb : Q2_AM178_BLUE_BACKGROUND;
            assert(s->pixels[y * s->width + x] == expected);
        }
    }
}
int main(void) {
    for (unsigned direction = 0; direction < 2; ++direction) {
        screen_t s = {.width = direction ? 448 : 368, .height = direction ? 368 : 448};
        s.pixels = calloc(s.width * s.height, sizeof(*s.pixels));
        assert(s.pixels);
        unsigned clock;
        assert(q2_am178_blue_step(0, s.width, s.height, &s.center, &clock) == Q2_OK);
        q2_pixel_backend_t b = {Q2_PIXEL_ABI, sizeof(b), &s, s.width, s.height, capture};
        assert(q2_am178_blue_baseline(&b) == Q2_OK);
        verify(&s, Q2_AM178_BLUE_BACKGROUND);
        assert(s.calls == (s.height + 31u) / 32u); /* No center patch during background fill. */
        s.only_center = true;
        for (unsigned i = 0; i < Q2_AM178_BLUE_STEPS; ++i) {
            q2_am178_area_step_t patch;
            assert(q2_am178_blue_step(i, s.width, s.height, &patch, &clock) == Q2_OK);
            assert(q2_am178_blue_patch(&b, &patch) == Q2_OK);
            verify(&s, patch.rgb);
            patch.rgb = 0x0000a0;
            assert(q2_am178_blue_patch(&b, &patch) == Q2_OK);
            verify(&s, patch.rgb);
        }
        s.calls = 0;
        s.fail_at = 1;
        assert(q2_am178_blue_patch(&b, &s.center) == Q2_ERR_IO && s.calls == 1);
        s.calls = 0;
        assert(q2_am178_blue_baseline(&b) == Q2_ERR_IO && s.calls == 1);
        q2_am178_area_step_t invalid = s.center;
        invalid.x = b.width;
        assert(q2_am178_blue_patch(&b, &invalid) == Q2_ERR_ARGUMENT);
        assert(q2_am178_blue_patch(NULL, &s.center) == Q2_ERR_ARGUMENT);
        assert(q2_am178_blue_patch(&b, NULL) == Q2_ERR_ARGUMENT);
        free(s.pixels);
    }
    puts("PASS: configured background preserved; center-only test/recovery; error propagation; both orientations");
}
