#ifndef Q2_AM178_GEOMETRY_H
#define Q2_AM178_GEOMETRY_H
#include <stdbool.h>
#include <stdint.h>
#include "panel_profile.h"
/* Historical internal names remain compatible with diagnostic callers. */
#define Q2_AM178_WIDTH Q2_PANEL_NATIVE_WIDTH
#define Q2_AM178_HEIGHT Q2_PANEL_NATIVE_HEIGHT
#define Q2_AM178_X_GAP Q2_PANEL_X_GAP
/** Validate logical dimensions for a clockwise device/view rotation of 0/90/180/270 degrees.
 * Portrait 0/180 uses native profile dimensions; landscape 90/270 swaps them. */
bool q2_am178_dimensions(unsigned width, unsigned height, unsigned rotation);
/** Map a logical pixel into native glass coordinates. No hardware mirror/scan reversal.
 * Returns false for invalid rotation/dimensions, bounds or null outputs. */
bool q2_am178_native(unsigned width, unsigned height, unsigned rotation, unsigned x, unsigned y,
                     unsigned *nx, unsigned *ny);
/** Inverse display mapping for the selected CHSC6417/CST820 report; false means release/invalid arguments/report. */
bool q2_am178_contact(unsigned width, unsigned height, unsigned rotation, const uint8_t report[Q2_TOUCH_REPORT_BYTES],
                      int *x, int *y);
/** Convert Kernel 0x00RRGGBB pixels to host-order RGB565; transport sends high byte first. */
uint16_t q2_am178_rgb565(uint32_t pixel);
#endif
