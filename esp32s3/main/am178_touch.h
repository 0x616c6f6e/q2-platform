#ifndef Q2_AM178_TOUCH_H
#define Q2_AM178_TOUCH_H
#include "q2/result.h"
#include "panel_profile.h"
#include <stdbool.h>
#include <stdint.h>

/* Acquisition-task-only state: an IRQ starts one bounded read burst; retries never require a new IRQ. */
#define Q2_AM178_TOUCH_ATTEMPTS 3u
#define Q2_AM178_TOUCH_RETRY_MS 5u
typedef struct {
    bool pending, pressed;
    unsigned attempts;
    uint64_t retry_at_ms;
    int x, y;
    uint32_t reads, retries, recoveries, failures;
} q2_am178_touch_state_t;
typedef q2_result_t (*q2_am178_touch_reader_t)(void *context, uint8_t report[Q2_TOUCH_REPORT_BYTES]);

/** Process a new IRQ or a due retry; at most one read per call.
 * Rotation/dimensions must match the display and remain fixed for the state lifetime.
 * Zero-initialize state. IO/timeout errors retry at least 5 ms later, at most three total reads.
 * During retries, return Q2_OK and the last valid contact (never synthesize a release).
 * Exhausted/permanent failures release the contact and return the read error.
 * Idle calls return cached coordinates without touching the bus. Single acquisition task only. */
q2_result_t q2_am178_touch_sample(q2_am178_touch_state_t *state, unsigned width, unsigned height, unsigned rotation,
                                  bool irq, uint64_t now_ms, q2_am178_touch_reader_t reader, void *context,
                                  bool *pressed, int *x, int *y);
#endif
