#ifndef Q2_S3_AM178_H
#define Q2_S3_AM178_H
#include "q2/lvgl.h"
typedef struct q2_am178 q2_am178_t;
/** Initialize the selected AM178/AM213 display and optional touch on the owner task. Out remains NULL on failure.
 * rotation is clockwise device/view orientation: 0/90/180/270. Dimensions must match it.
 * Returns Q2_ERR_ARGUMENT for mismatched dimensions or unsupported angles. Requires PSRAM. TP INT uses falling-edge events; LCD TE synchronization is disabled. */
q2_result_t q2_am178_create(unsigned width, unsigned height, unsigned rotation, q2_am178_t **out);
/** Diagnostic creation with an explicit 2 MHz or 10 MHz QSPI clock. Other initialization
 * settings and rotation are identical to create(). Owner only; no existing panel may own SPI2.
 * Returns Q2_ERR_ARGUMENT for other clocks, or the same initialization errors as create(). */
q2_result_t q2_am178_create_with_clock(unsigned width, unsigned height, unsigned rotation,
                                       unsigned clock_hz, q2_am178_t **out);
/** Diagnostic owner-only solid full-frame write, bypassing LVGL. RGB is 0xRRGGBB.
 * Covers the native frame with internal DMA bands and waits for each completion.
 * On I/O/timeout the panel is marked failed and buffers remain retained until teardown. */
q2_result_t q2_am178_solid_rgb(q2_am178_t *panel, uint32_t rgb);
/** Borrow synchronous pixel backend; panel must outlive the Kernel display provider. */
q2_result_t q2_am178_backend(q2_am178_t *panel, q2_pixel_backend_t *out);
/** Fill a caller-owned control descriptor before boot; descriptor and panel must outlive the Kernel.
 * Lua panel.brightness([percent]) requires display.brightness. Range 0..100; no argument reads
 * the last transmitted setting. Owner only; failures preserve the cached value. Not persistent. */
q2_result_t q2_am178_controls(q2_am178_t *panel, q2_component_t *out);
/** Owner-only hardware access. write=true sends *percent (0..100), false reads the cache.
 * Returns Q2_ERR_ARGUMENT for invalid parameters; I/O errors preserve the cached setting. */
q2_result_t q2_am178_brightness(q2_am178_t *panel, bool write, unsigned *percent);
/** Pop the oldest acquired touch sample without I2C or blocking on the owner task.
 * Q2_ERR_NOT_FOUND means the FIFO is empty; it MUST NOT be interpreted as a release.
 * A dedicated task handles IRQ reads and bounded retries; each valid press/release is queued in order.
 * Exhausted I/O retries enqueue a release with the error status. Motion may coalesce under pressure.
 * Q2_ERR_UNSUPPORTED means acquisition did not initialize. */
q2_result_t q2_am178_touch(q2_am178_t *panel, bool *pressed, int *x, int *y);
/** Release hardware after the display provider has stopped. NULL is accepted. */
q2_result_t q2_am178_destroy(q2_am178_t **panel);
#endif
