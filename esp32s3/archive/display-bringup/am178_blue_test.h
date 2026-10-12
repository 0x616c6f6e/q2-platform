#ifndef Q2_AM178_BLUE_TEST_H
#define Q2_AM178_BLUE_TEST_H
#include "am178_area_test.h"
#define Q2_AM178_BLUE_LEVELS 9u
#define Q2_AM178_BLUE_CLOCK_GROUPS 1u
#define Q2_AM178_BLUE_BACKGROUND 0x000000u
#define Q2_AM178_BLUE_STEPS (Q2_AM178_BLUE_CLOCK_GROUPS * Q2_AM178_BLUE_LEVELS)
#if Q2_AM178_BLUE_CLOCK_GROUPS < 1 || Q2_AM178_BLUE_CLOCK_GROUPS > 2
#error "Use one clock group (10 MHz) or two (10 MHz then 2 MHz)"
#endif
/** Select a centered ~1% palette rectangle and configured clock group.
 * Uses even coordinates; reference background and recovery distinguish local from whole-panel failures.
 * Returns ARGUMENT for invalid output/dimensions, NOT_FOUND for an index beyond the sequence. */
q2_result_t q2_am178_blue_step(unsigned index, unsigned width, unsigned height,
                               q2_am178_area_step_t *step, unsigned *clock_hz);
/** Write only the requested rectangle through the synchronous backend; surrounding pixels are untouched.
 * Owner-only; returns ARGUMENT for invalid geometry/backend, MEMORY for allocation failure,
 * or the first backend error. All temporary pixel memory is released before returning. */
q2_result_t q2_am178_blue_patch(const q2_pixel_backend_t *backend, const q2_am178_area_step_t *step);
/** Draw only the configured full reference background, without any center overlay.
 * Owner-only; same errors/lifetime as blue_patch(). Does not reset/reinitialize the panel. */
q2_result_t q2_am178_blue_baseline(const q2_pixel_backend_t *backend);
/** Run the isolated hardware diagnostic on the owner task. Returns only on error.
 * Each case recreates/resets the panel; success means transfers completed, not a visual pass.
 * Uses the configured startup brightness (0..100%) for every case, with no hidden override. */
void q2_am178_blue_demo(unsigned width, unsigned height, unsigned rotation);
#endif
