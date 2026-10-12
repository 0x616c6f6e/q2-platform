/* Exercise production wire packing and framebuffer-to-DMA row copies in both formats. */
#include "am178_pixel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    uint8_t packed[5];
    for (unsigned c = 0; c < 256; ++c) {
        const uint32_t colors[] = {c << 16, c << 8, c, (c << 16) | (c << 8) | c};
        for (unsigned i = 0; i < 4; ++i) {
            memset(packed, 0xa5, sizeof(packed));
            uint32_t rgb = colors[i];
            q2_am178_pack_pixel(packed + 1, rgb);
            assert(packed[0] == 0xa5 && packed[1 + Q2_AM178_PIXEL_BYTES] == 0xa5);
#if Q2_AM178_PIXEL_BITS == 24
            assert(packed[1] == (rgb >> 16));
            assert(packed[2] == ((rgb >> 8) & 255));
            assert(packed[3] == (rgb & 255));
#else
            unsigned pixel = (((rgb >> 16) >> 3) << 11) |
                             ((((rgb >> 8) & 255) >> 2) << 5) | ((rgb & 255) >> 3);
            assert(packed[1] == (pixel >> 8) && packed[2] == (pixel & 255));
#endif
        }
    }
    uint8_t f8[3], ff[3];
    q2_am178_pack_pixel(f8, 0x0000f8);
    q2_am178_pack_pixel(ff, 0x0000ff);
#if Q2_AM178_PIXEL_BITS == 24
    assert(f8[0] == 0 && f8[1] == 0 && f8[2] == 0xf8);
    assert(ff[0] == 0 && ff[1] == 0 && ff[2] == 0xff);
#else
    assert(f8[0] == 0 && f8[1] == 0x1f && memcmp(f8, ff, 2) == 0);
#endif
    size_t bytes = Q2_AM178_WIDTH * Q2_AM178_HEIGHT * Q2_AM178_PIXEL_BYTES;
    uint8_t *frame = malloc(bytes);
    assert(frame);
    for (unsigned y = 0; y < Q2_AM178_HEIGHT; ++y)
        for (unsigned x = 0; x < Q2_AM178_WIDTH; ++x)
            q2_am178_pack_pixel(frame + (y * Q2_AM178_WIDTH + x) * Q2_AM178_PIXEL_BYTES,
                                ((x & 255) << 16) | ((y & 255) << 8) | ((x + y) & 255));
    uint8_t tile[Q2_AM178_WIDTH * Q2_AM178_PIXEL_BYTES + 2];
    const unsigned regions[][3] = {{0, 0, Q2_AM178_WIDTH}, {Q2_AM178_WIDTH - 2, Q2_AM178_HEIGHT - 1, 2}, {16, 222, 300}};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned x = regions[i][0], y = regions[i][1], width = regions[i][2];
        memset(tile, 0xa5, sizeof(tile));
        q2_am178_copy_pixel_row(tile + 1, frame, x, y, width);
        assert(tile[0] == 0xa5 && tile[1 + width * Q2_AM178_PIXEL_BYTES] == 0xa5);
        for (unsigned j = 0; j < width; ++j) {
            q2_am178_pack_pixel(packed, (((x + j) & 255) << 16) | ((y & 255) << 8) |
                                        ((x + j + y) & 255));
            assert(memcmp(tile + 1 + j * Q2_AM178_PIXEL_BYTES, packed, Q2_AM178_PIXEL_BYTES) == 0);
        }
    }
    free(frame);
    printf("%s packing, F8/FF distinction, row offsets and guards passed\n", Q2_AM178_PIXEL_NAME);
    return 0;
}
