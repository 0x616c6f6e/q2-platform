#include "key_input.h"
#include "q2_platform_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        checks++;                                                                                            \
        if (!(expr)) {                                                                                       \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr);                                          \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
typedef struct {
    q2_key_action_t actions[16];
    unsigned count;
    q2_result_t result;
} sink_t;
static q2_result_t emit(void *context, q2_key_action_t action) {
    sink_t *sink = context;
    CHECK(sink->count < 16);
    sink->actions[sink->count++] = action;
    return sink->result;
}
static void reset(q2_pc_key_input_t *key, sink_t *sink) {
    CHECK(q2_pc_key_cancel(key) == Q2_OK);
    memset(sink, 0, sizeof(*sink));
}
#define EDGE(down, time) CHECK(q2_pc_key_edge(&key, down, time, emit, &sink) == Q2_OK)
#define POLL(time) CHECK(q2_pc_key_poll(&key, time, emit, &sink) == Q2_OK)
int main(void) {
    q2_pc_key_input_t key = {0};
    sink_t sink = {0};
    const uint64_t m = Q2_KEY_SINGLE_MIN_MS, g = Q2_KEY_DOUBLE_GAP_MS, l = Q2_KEY_LONG_PRESS_MS;
    EDGE(true, 0);
    EDGE(false, m);
    POLL(m + g - 1);
    CHECK(sink.count == 0);
    POLL(m + g);
    CHECK(sink.count == 1 && sink.actions[0] == Q2_KEY_SINGLE);
    POLL(m + g + l);
    CHECK(sink.count == 1);

    /* The second press, not its release, must fall inside the double-click window. */
    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m);
    EDGE(true, m + g - 1);
    EDGE(false, 2 * m + g - 1);
    CHECK(sink.count == 1 && sink.actions[0] == Q2_KEY_DOUBLE);
    POLL(2 * m + 2 * g);
    CHECK(sink.count == 1);
    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m);
    EDGE(true, m + g);
    CHECK(sink.count == 1 && sink.actions[0] == Q2_KEY_SINGLE);
    EDGE(false, 2 * m + g);
    POLL(2 * m + 2 * g);
    CHECK(sink.count == 2 && sink.actions[1] == Q2_KEY_SINGLE);

    reset(&key, &sink);
    EDGE(true, 0);
    POLL(l - 1);
    CHECK(sink.count == 0);
    POLL(l);
    CHECK(sink.count == 1 && sink.actions[0] == Q2_KEY_LONG);
    EDGE(true, l + 1);
    POLL(2 * l);
    EDGE(false, 2 * l + 1);
    POLL(3 * l + g);
    CHECK(sink.count == 1); /* No hold repeats or release click after LONG. */
    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, l);
    CHECK(sink.count == 1 && sink.actions[0] == Q2_KEY_LONG);

    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m - 1);
    POLL(m + g);
    CHECK(sink.count == 0); /* Brief pulse is not a click. */
    EDGE(true, m + g + 1);
    EDGE(false, m + g + 1 + l - 1);
    POLL(m + 2 * g + l);
    CHECK(sink.count == 1 && sink.actions[0] == Q2_KEY_SINGLE);

    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m);
    EDGE(true, m + 1);
    EDGE(false, 2 * m);
    POLL(m + g);
    CHECK(sink.count == 1 && sink.actions[0] == Q2_KEY_SINGLE);
    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m);
    EDGE(true, m + 1);
    POLL(m + 1 + l);
    CHECK(sink.count == 2 && sink.actions[0] == Q2_KEY_SINGLE && sink.actions[1] == Q2_KEY_LONG);
    EDGE(false, m + 1 + l);
    POLL(m + 1 + l + g);
    CHECK(sink.count == 2);

    reset(&key, &sink);
    EDGE(false, 0);
    EDGE(true, 1);
    EDGE(true, 2);
    EDGE(false, m + 1);
    EDGE(false, m + 2);
    POLL(m + 1 + g);
    CHECK(sink.count == 1);
    reset(&key, &sink);
    EDGE(true, 0);
    CHECK(q2_pc_key_cancel(&key) == Q2_OK);
    POLL(l + g);
    CHECK(sink.count == 0 && !key.down);
    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m);
    CHECK(q2_pc_key_cancel(&key) == Q2_OK);
    POLL(l + g);
    CHECK(sink.count == 0);

    /* One key's pending click and another key's hold have independent clocks. */
    reset(&key, &sink);
    q2_pc_key_input_t other = {0};
    sink_t other_sink = {0};
    EDGE(true, 0);
    EDGE(false, m);
    CHECK(q2_pc_key_edge(&other, true, 0, emit, &other_sink) == Q2_OK);
    POLL(m + g);
    CHECK(sink.count == 1 && other_sink.count == 0);
    CHECK(q2_pc_key_poll(&other, l, emit, &other_sink) == Q2_OK);
    CHECK(other_sink.count == 1 && other_sink.actions[0] == Q2_KEY_LONG);

    /* Queue-full delivery is consumed once, including two actions at the same tick. */
    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m);
    sink.result = Q2_ERR_LIMIT;
    CHECK(q2_pc_key_poll(&key, m + g, emit, &sink) == Q2_ERR_LIMIT);
    POLL(m + g + 1);
    CHECK(sink.count == 1);
    reset(&key, &sink);
    EDGE(true, 0);
    EDGE(false, m);
    EDGE(true, m + 1);
    sink.result = Q2_ERR_LIMIT;
    CHECK(q2_pc_key_poll(&key, m + 1 + l, emit, &sink) == Q2_ERR_LIMIT);
    POLL(m + 2 + l);
    CHECK(sink.count == 2);
    reset(&key, &sink);
    uint64_t base = UINT64_C(1) << 40;
    EDGE(true, base);
    EDGE(false, base + m);
    POLL(base + m + g);
    CHECK(sink.count == 1);
    CHECK(q2_pc_key_edge(&key, true, base, emit, &sink) == Q2_ERR_ARGUMENT);
    CHECK(!key.down);
    CHECK(q2_pc_key_poll(NULL, 0, emit, &sink) == Q2_ERR_ARGUMENT);
    CHECK(q2_pc_key_edge(&key, true, base, NULL, &sink) == Q2_ERR_ARGUMENT);
    CHECK(q2_pc_key_cancel(NULL) == Q2_ERR_ARGUMENT);
    printf("PASS: %u x86 key timing checks\n", checks);
    return 0;
}
