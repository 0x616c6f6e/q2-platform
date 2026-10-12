#include "am178_geometry.h"
bool q2_am178_dimensions(unsigned w, unsigned h, unsigned rotation) {
    if (rotation == 0 || rotation == 180)
        return w == Q2_AM178_WIDTH && h == Q2_AM178_HEIGHT;
    if (rotation == 90 || rotation == 270)
        return w == Q2_AM178_HEIGHT && h == Q2_AM178_WIDTH;
    return false;
}
bool q2_am178_native(unsigned w, unsigned h, unsigned rotation, unsigned x, unsigned y,
                     unsigned *nx, unsigned *ny) {
    if (!nx || !ny || !q2_am178_dimensions(w, h, rotation) || x >= w || y >= h)
        return false;
    /* Inverse of rotating the physical device clockwise: logical canvas -> native glass. */
    switch (rotation) {
    case 0:
        *nx = x;
        *ny = y;
        break;
    case 90:
        *nx = y;
        *ny = Q2_AM178_HEIGHT - 1u - x;
        break;
    case 180:
        *nx = Q2_AM178_WIDTH - 1u - x;
        *ny = Q2_AM178_HEIGHT - 1u - y;
        break;
    case 270:
        *nx = Q2_AM178_WIDTH - 1u - y;
        *ny = x;
        break;
    }
    return true;
}
bool q2_am178_contact(unsigned w, unsigned h, unsigned rotation, const uint8_t b[Q2_TOUCH_REPORT_BYTES], int *x, int *y) {
    if (!b || !x || !y || !q2_am178_dimensions(w, h, rotation))
        return false;
#if Q2_PANEL_AM213
    /* CST820: count at 02h; 12-bit X/Y at 03h..06h. Reject malformed/multiple contacts. */
    if (b[2] != 1)
        return false;
    unsigned nx = ((unsigned)(b[3] & 0x0f) << 8) | b[4];
    unsigned ny = ((unsigned)(b[5] & 0x0f) << 8) | b[6];
#else
    if ((b[0] & 3) != 1)
        return false;
    unsigned nx = ((b[0] & 0x40) << 2) | b[1];
    unsigned ny = ((b[0] & 0x80) << 1) | b[2];
#endif
    if (nx >= Q2_AM178_WIDTH || ny >= Q2_AM178_HEIGHT)
        return false;
    /* Touch reports stay in native glass coordinates, so apply the forward rotation. */
    switch (rotation) {
    case 0:
        *x = (int)nx;
        *y = (int)ny;
        break;
    case 90:
        *x = (int)(Q2_AM178_HEIGHT - 1u - ny);
        *y = (int)nx;
        break;
    case 180:
        *x = (int)(Q2_AM178_WIDTH - 1u - nx);
        *y = (int)(Q2_AM178_HEIGHT - 1u - ny);
        break;
    case 270:
        *x = (int)ny;
        *y = (int)(Q2_AM178_WIDTH - 1u - nx);
        break;
    }
    return true;
}
uint16_t q2_am178_rgb565(uint32_t p) {
    return (uint16_t)(((p >> 8) & 0xf800) | ((p >> 5) & 0x07e0) | ((p >> 3) & 0x001f));
}
