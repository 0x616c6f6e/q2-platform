#ifndef Q2_AM178_AREA_TEST_H
#define Q2_AM178_AREA_TEST_H
#include "q2/lvgl.h"
#define Q2_AM178_AREA_STEPS 42u
/** One centered rectangle on a black background. RGB is 0xRRGGBB; percentage is the target area. */
typedef struct {
    unsigned x, y, width, height, target_percent;
    uint32_t rgb;
    const char *color_name;
} q2_am178_area_step_t;
/** Produce an A0/FF pair per R/G/B color for each increasing area (1,5,10,25,50,75,100%).
 * Even coordinates/extents avoid extra illuminated pixels from panel window rounding.
 * Returns ARGUMENT for invalid output/dimensions, NOT_FOUND after the last step. */
q2_result_t q2_am178_area_step(unsigned index, unsigned width, unsigned height,
                               q2_am178_area_step_t *out);
/** Send one complete black-background frame through the synchronous pixel backend.
 * Caller keeps brightness fixed. No text, timers, touch or persistent state are involved.
 * Returns ARGUMENT for invalid bounds/backend, MEMORY on allocation failure, or the backend error. */
q2_result_t q2_am178_area_render(const q2_pixel_backend_t *backend, const q2_am178_area_step_t *step);
#endif
