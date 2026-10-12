#include "am178_touch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, calls;
static q2_result_t response;
static uint8_t report[Q2_TOUCH_REPORT_BYTES];
#define CHECK(e) do { checks++; if (!(e)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #e); exit(1); } } while (0)
static q2_result_t read_report(void *context, uint8_t out[Q2_TOUCH_REPORT_BYTES]) {
    (void)context;
    calls++;
    memcpy(out, report, Q2_TOUCH_REPORT_BYTES);
    return response;
}
static void set_report(bool down, unsigned x, unsigned y) {
    memset(report, 0, sizeof(report));
#if Q2_PANEL_AM213
    report[2] = down; report[3] = x >> 8; report[4] = x; report[5] = y >> 8; report[6] = y;
#else
    report[0] = down | ((x >> 8) << 6) | ((y >> 8) << 7); report[1] = x; report[2] = y;
#endif
}
int main(void) {
    q2_am178_touch_state_t s = {0};
    bool pressed;
    int x, y;
#define SAMPLE(irq, time) q2_am178_touch_sample(&s, Q2_PANEL_NATIVE_WIDTH, Q2_PANEL_NATIVE_HEIGHT, 0, irq, time, read_report, NULL, &pressed, &x, &y)
    CHECK(SAMPLE(false, 0) == Q2_OK && calls == 0 && !pressed);
    set_report(true, 100, 200);
    CHECK(SAMPLE(true, 10) == Q2_OK && calls == 1 && pressed && x == 100 && y == 200);
    CHECK(SAMPLE(false, 20) == Q2_OK && calls == 1 && pressed);
    /* An intermittent NACK must not create a release/click or need a second IRQ to recover. */
    response = Q2_ERR_IO;
    CHECK(SAMPLE(true, 30) == Q2_OK && calls == 2 && pressed && s.pending);
    CHECK(SAMPLE(false, 34) == Q2_OK && calls == 2 && pressed);
    response = Q2_OK; set_report(true, 110, 200);
    CHECK(SAMPLE(false, 35) == Q2_OK && calls == 3 && pressed && x == 110 && !s.pending);
    CHECK(s.recoveries == 1 && s.retries == 1 && s.failures == 0);
    /* Retrying a release report retains the press only until a valid release arrives. */
    response = Q2_ERR_TIMEOUT;
    CHECK(SAMPLE(true, 40) == Q2_OK && calls == 4 && pressed);
    response = Q2_OK; set_report(false, 110, 200);
    CHECK(SAMPLE(false, 45) == Q2_OK && calls == 5 && !pressed);
    CHECK(s.recoveries == 2);
    /* New IRQs during failure cannot reset the attempt budget or postpone the terminal release. */
    set_report(true, 110, 200);
    CHECK(SAMPLE(true, 50) == Q2_OK && pressed);
    response = Q2_ERR_IO;
    CHECK(SAMPLE(true, 60) == Q2_OK && pressed && s.attempts == 1);
    unsigned before = calls;
    CHECK(SAMPLE(true, 61) == Q2_OK && pressed && calls == before && s.attempts == 1);
    CHECK(SAMPLE(true, 65) == Q2_OK && pressed && s.attempts == 2);
    CHECK(SAMPLE(true, 70) == Q2_ERR_IO && !pressed && !s.pending && s.attempts == 3);
    CHECK(s.failures == 1);
    before = calls;
    CHECK(SAMPLE(false, 1000) == Q2_OK && !pressed && calls == before);
    /* A later real IRQ can restart acquisition; configuration errors are not retried. */
    response = Q2_OK;
    CHECK(SAMPLE(true, 1001) == Q2_OK && pressed && s.attempts == 1);
    response = Q2_ERR_ARGUMENT;
    CHECK(SAMPLE(true, 1002) == Q2_ERR_ARGUMENT && !pressed && !s.pending);
    CHECK(s.failures == 2);
    /* Initial NACK does not invent a down event, and retry success reports the first touch. */
    memset(&s, 0, sizeof(s));
    response = Q2_ERR_IO;
    CHECK(SAMPLE(true, 2000) == Q2_OK && !pressed);
    response = Q2_OK;
    CHECK(SAMPLE(false, 2005) == Q2_OK && pressed && x == 110 && y == 200);
    CHECK(q2_am178_touch_sample(NULL, Q2_PANEL_NATIVE_WIDTH, Q2_PANEL_NATIVE_HEIGHT, 0, false, 0, read_report, NULL, &pressed, &x, &y) == Q2_ERR_ARGUMENT);
    /* Verify the acquisition layer passes each explicit angle through, including 180/270. */
#if Q2_PANEL_AM213
    const int expected[4][2] = {{100, 200}, {301, 100}, {309, 301}, {200, 309}};
#else
    const int expected[4][2] = {{100, 200}, {247, 100}, {267, 247}, {200, 267}};
#endif
    for (unsigned i = 0; i < 4; ++i) {
        memset(&s, 0, sizeof(s));
        response = Q2_OK;
        set_report(true, 100, 200);
        unsigned w = i % 2 ? Q2_PANEL_NATIVE_HEIGHT : Q2_PANEL_NATIVE_WIDTH,
                 h = i % 2 ? Q2_PANEL_NATIVE_WIDTH : Q2_PANEL_NATIVE_HEIGHT;
        CHECK(q2_am178_touch_sample(&s, w, h, i * 90, true, 0, read_report, NULL,
                                     &pressed, &x, &y) == Q2_OK);
        CHECK(pressed && x == expected[i][0] && y == expected[i][1]);
    }
    printf("PASS: %u touch retry checks\n", checks);
    return 0;
}
