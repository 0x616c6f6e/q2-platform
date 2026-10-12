#include "am178_touch.h"
#include "am178_geometry.h"
#include <stddef.h>

q2_result_t q2_am178_touch_sample(q2_am178_touch_state_t *s, unsigned w, unsigned h, unsigned rotation,
                                  bool irq,
                                  uint64_t now, q2_am178_touch_reader_t reader, void *context,
                                  bool *pressed, int *x, int *y) {
    if (!s || !reader || !pressed || !x || !y || !q2_am178_dimensions(w, h, rotation))
        return Q2_ERR_ARGUMENT;
    /* A newer IRQ coalesces with an ongoing retry; it must not reset the failure budget. */
    if (irq && !s->pending) {
        s->pending = true;
        s->attempts = 0;
        s->retry_at_ms = now;
    }
    q2_result_t e = Q2_OK;
    if (s->pending && now >= s->retry_at_ms) {
        uint8_t report[Q2_TOUCH_REPORT_BYTES] = {0};
        s->attempts++;
        s->reads++;
        e = reader(context, report);
        if (e == Q2_OK) {
            if (s->attempts > 1)
                s->recoveries++;
            s->pending = false;
            s->pressed = q2_am178_contact(w, h, rotation, report, &s->x, &s->y);
        } else if ((e == Q2_ERR_IO || e == Q2_ERR_TIMEOUT) && s->attempts < Q2_AM178_TOUCH_ATTEMPTS) {
            /* Preserve a held finger across a transient bus error. Yield to UI until retry is due. */
            s->retries++;
            s->retry_at_ms = now + Q2_AM178_TOUCH_RETRY_MS;
            e = Q2_OK;
        } else {
            /* Never retain a pressed pointer indefinitely when the device stops responding. */
            s->pending = false;
            s->pressed = false;
            s->failures++;
        }
    }
    *pressed = s->pressed;
    *x = s->x;
    *y = s->y;
    return e;
}
