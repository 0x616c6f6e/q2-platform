/* Exercise the production flush body against independent panel RAM and coordinate oracles.
 * Unlike solid-color tests, every pixel depends on x/y; incorrect stride, rotation or window
 * dimensions must differ. The extracted function is generated from am178.c by the test runner. */
#include "am178_pixel.h"
#include "board.h"
#include "q2/result.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TILE_ROWS Q2_LCD_TILE_ROWS
#define FRAME_BYTES (Q2_AM178_WIDTH * Q2_AM178_HEIGHT * Q2_AM178_PIXEL_BYTES)
#define TILE_BYTES (Q2_AM178_WIDTH * TILE_ROWS * Q2_AM178_PIXEL_BYTES)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
typedef struct {
    uint8_t *frame, *tile;
    unsigned width, height, rotation;
    bool failed;
} q2_am178_t;
static uint8_t panel_ram[FRAME_BYTES];
static uint32_t *expected;
static unsigned draws, fail_at;
static q2_result_t result(esp_err_t e) {
    return e == ESP_OK ? Q2_OK : Q2_ERR_IO;
}
static esp_err_t draw_band(q2_am178_t *s, unsigned x, unsigned y, unsigned w, unsigned h) {
    assert(!(x & 1) && !(y & 1) && !(w & 1) && !(h & 1));
    assert(w && h && x + w <= Q2_AM178_WIDTH && y + h <= Q2_AM178_HEIGHT);
    assert(w * h * Q2_AM178_PIXEL_BYTES <= TILE_BYTES);
    if (++draws == fail_at)
        return -1;
    /* A panel consumes tightly packed rows in the specified window; it knows no host stride. */
    for (unsigned j = 0; j < h; j++)
        memcpy(panel_ram + ((y + j) * Q2_AM178_WIDTH + x) * 2, s->tile + j * w * 2, w * 2);
    return ESP_OK;
}
#include "am178_flush_under_test.inc"
static uint32_t pattern(unsigned x, unsigned y, unsigned seed) {
    return (((x * 17 + y + seed) & 255) << 16) | (((y * 13 + seed) & 255) << 8) | ((x + y * 3 + seed) & 255);
}
static void verify(q2_am178_t *s) {
    for (unsigned ny = 0; ny < Q2_AM178_HEIGHT; ny++) {
        for (unsigned nx = 0; nx < Q2_AM178_WIDTH; nx++) {
            /* Forward transform from panel coordinates, independent of q2_am178_native(). */
            unsigned x = nx, y = ny;
            switch (s->rotation) {
            case 90:
                x = Q2_AM178_HEIGHT - 1 - ny;
                y = nx;
                break;
            case 180:
                x = Q2_AM178_WIDTH - 1 - nx;
                y = Q2_AM178_HEIGHT - 1 - ny;
                break;
            case 270:
                x = ny;
                y = Q2_AM178_WIDTH - 1 - nx;
                break;
            }
            uint32_t p = expected[y * s->width + x];
            unsigned r = ((p >> 16) & 255) / 8, g = ((p >> 8) & 255) / 4, b = (p & 255) / 8;
            unsigned value = r * 2048 + g * 32 + b;
            size_t offset = (ny * Q2_AM178_WIDTH + nx) * 2;
            assert(panel_ram[offset] == value / 256 && panel_ram[offset + 1] == value % 256);
        }
    }
}
static void update(q2_am178_t *s, unsigned x, unsigned y, unsigned w, unsigned h, unsigned seed) {
    /* Padded source rows ensure that a mistaken width-based stride cannot pass. */
    size_t stride = (w + 3) * sizeof(uint32_t);
    uint32_t *source = malloc(h * stride);
    assert(source);
    memset(source, 0xac, h * stride);
    for (unsigned j = 0; j < h; j++)
        for (unsigned i = 0; i < w; i++)
            source[j * (w + 3) + i] = expected[(y + j) * s->width + x + i] = pattern(x + i, y + j, seed);
    assert(flush(s, x, y, w, h, source, stride) == Q2_OK);
    free(source);
    verify(s);
}
int main(void) {
    expected = malloc(Q2_AM178_WIDTH * Q2_AM178_HEIGHT * sizeof(*expected));
    assert(expected);
    for (unsigned rotation = 0; rotation < 360; rotation += 90) {
        q2_am178_t s = {.frame = calloc(1, FRAME_BYTES), .tile = malloc(TILE_BYTES), .rotation = rotation};
        assert(s.frame && s.tile);
        s.width = rotation % 180 ? Q2_AM178_HEIGHT : Q2_AM178_WIDTH;
        s.height = rotation % 180 ? Q2_AM178_WIDTH : Q2_AM178_HEIGHT;
        draws = fail_at = 0;
        memset(panel_ram, 0xa5, sizeof(panel_ram));
        update(&s, 0, 0, s.width, s.height, 1);
        /* One complete logical frame must become one pass of native row bands in every orientation. */
        assert(draws == (Q2_AM178_HEIGHT + TILE_ROWS - 1) / TILE_ROWS);
        update(&s, 1, 3, 17, 13, 27);
        update(&s, s.width - 1, s.height - 1, 1, 1, 73);
        update(&s, s.width / 2 - 1, 7, 31, 81, 110);
        update(&s, 0, 0, 1, 1, 159);
        unsigned before = draws;
        assert(flush(&s, s.width - 1, 0, 2, 1, expected, 8) == Q2_ERR_ARGUMENT);
        assert(flush(&s, 0, 0, 4, 1, expected, 12) == Q2_ERR_ARGUMENT);
        assert(draws == before);
        fail_at = draws + 2;
        assert(flush(&s, 0, 0, s.width, s.height, expected, s.width * 4) == Q2_ERR_IO);
        before = draws;
        assert(flush(&s, 0, 0, s.width, s.height, expected, s.width * 4) == Q2_ERR_STATE);
        assert(draws == before);
        free(s.frame);
        free(s.tile);
    }
    free(expected);
    puts("PASS: production flush, 4 rotations, nonuniform frames, odd dirty areas, stride and I/O errors");
    return 0;
}
