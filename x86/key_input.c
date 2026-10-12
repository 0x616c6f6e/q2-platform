#include "key_input.h"
#include "q2_platform_config.h"
#include <string.h>

q2_result_t q2_pc_key_poll(q2_pc_key_input_t *key, uint64_t now, q2_pc_key_emit_fn emit, void *context) {
    if (!key || !emit || now < key->last_tick)
        return Q2_ERR_ARGUMENT;
    key->last_tick = now;
    if (key->down && !key->long_sent && now - key->pressed_at >= Q2_KEY_LONG_PRESS_MS) {
        /* A short first click followed by a held second press is SINGLE then LONG. */
        bool first = key->pending;
        key->pending = key->second = false;
        key->long_sent = true;
        q2_result_t e = first ? emit(context, Q2_KEY_SINGLE) : Q2_OK;
        q2_result_t next = emit(context, Q2_KEY_LONG);
        return e != Q2_OK ? e : next;
    }
    if (!key->down && key->pending && now - key->released_at >= Q2_KEY_DOUBLE_GAP_MS) {
        key->pending = false;
        return emit(context, Q2_KEY_SINGLE);
    }
    return Q2_OK;
}
q2_result_t q2_pc_key_edge(q2_pc_key_input_t *key, bool down, uint64_t now, q2_pc_key_emit_fn emit,
                           void *context) {
    if (!key || !emit || now < key->last_tick)
        return Q2_ERR_ARGUMENT;
    /* Resolve expired timers before applying an edge, including a release at LONG. */
    q2_result_t e = q2_pc_key_poll(key, now, emit, context);
    if (down == key->down)
        return e;
    key->down = down;
    if (down) {
        key->pressed_at = now;
        key->long_sent = false;
        key->second = key->pending;
    } else if (!key->long_sent) {
        if (now - key->pressed_at < Q2_KEY_SINGLE_MIN_MS) {
            /* A brief second pulse must not eat the first valid click. */
            key->second = false;
            q2_result_t next = q2_pc_key_poll(key, now, emit, context);
            return e != Q2_OK ? e : next;
        }
        if (key->second) {
            key->pending = key->second = false;
            q2_result_t next = emit(context, Q2_KEY_DOUBLE);
            return e != Q2_OK ? e : next;
        }
        key->pending = true;
        key->released_at = now;
    }
    return e;
}
q2_result_t q2_pc_key_cancel(q2_pc_key_input_t *key) {
    if (!key)
        return Q2_ERR_ARGUMENT;
    memset(key, 0, sizeof(*key));
    return Q2_OK;
}
