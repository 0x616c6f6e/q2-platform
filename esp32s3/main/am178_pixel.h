#ifndef Q2_AM178_PIXEL_H
#define Q2_AM178_PIXEL_H
#include "am178_config.h"
#include "am178_geometry.h"
#include <string.h>
/** Pack one 0x00RRGGBB pixel into Q2_AM178_PIXEL_BYTES wire-order bytes.
 * Owner-only internal helper; out must have the selected byte capacity.
 * RGB888 preserves each 8-bit channel; RGB565 sends its high byte first. */
static inline void q2_am178_pack_pixel(uint8_t *out, uint32_t rgb) {
#if Q2_AM178_PIXEL_BITS == 24
    out[0] = (uint8_t)(rgb >> 16);
    out[1] = (uint8_t)(rgb >> 8);
    out[2] = (uint8_t)rgb;
#else
    uint16_t pixel = q2_am178_rgb565(rgb);
    out[0] = (uint8_t)(pixel >> 8);
    out[1] = (uint8_t)pixel;
#endif
}
/** Copy a native framebuffer row segment into a DMA tile without byte swapping.
 * Caller validates coordinates and allocates width * Q2_AM178_PIXEL_BYTES output bytes. */
static inline void q2_am178_copy_pixel_row(uint8_t *out, const uint8_t *frame,
                                          unsigned x, unsigned y, unsigned width) {
    memcpy(out, frame + (y * Q2_AM178_WIDTH + x) * Q2_AM178_PIXEL_BYTES,
           width * Q2_AM178_PIXEL_BYTES);
}
#endif
