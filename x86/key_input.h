#ifndef Q2_PC_KEY_INPUT_H
#define Q2_PC_KEY_INPUT_H
#include "q2/keys.h"
#include <stdbool.h>
#include <stdint.h>

/* One state per registered key, zero-initialized. Calls belong to the SoC owner. */
typedef struct {
    bool down, pending, second, long_sent;
    uint64_t pressed_at, released_at, last_tick;
} q2_pc_key_input_t;

/* The sink forwards a recognized action. Errors consume the action, without replay. */
typedef q2_result_t (*q2_pc_key_emit_fn)(void *context, q2_key_action_t action);

/** Accept a raw edge at a monotonic SoC timestamp (ms); repeated levels are harmless. */
q2_result_t q2_pc_key_edge(q2_pc_key_input_t *key, bool down, uint64_t now, q2_pc_key_emit_fn emit,
                           void *context);
/** Advance timers even without an edge; LONG fires while held, SINGLE after its wait. */
q2_result_t q2_pc_key_poll(q2_pc_key_input_t *key, uint64_t now, q2_pc_key_emit_fn emit, void *context);
/** Drop held/pending input after cancellation, without generating a click. */
q2_result_t q2_pc_key_cancel(q2_pc_key_input_t *key);
#endif
