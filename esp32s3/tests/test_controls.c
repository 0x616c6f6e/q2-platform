/* Real Kernel + production registration glue; substitute only physical panel I/O. */
#include "am178.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
struct q2_am178 { unsigned percent, calls; bool fail; };
q2_result_t q2_am178_brightness(q2_am178_t *s, bool write, unsigned *percent) {
    s->calls++;
    if (s->fail)
        return Q2_ERR_IO;
    if (write)
        s->percent = *percent;
    *percent = s->percent;
    return Q2_OK;
}
static void *allocate(void *ctx, size_t size) { (void)ctx; return malloc(size); }
static void release(void *ctx, void *ptr) { (void)ctx; free(ptr); }
static uint64_t now(void *ctx) { (void)ctx; return 0; }
int main(void) {
    const q2_port_t port = {Q2_PORT_ABI, sizeof(port), NULL, allocate, release, now};
    q2_kernel_t *kernel = NULL;
    q2_am178_t panel = {.percent = 30};
    q2_component_t control, broken;
    assert(q2_kernel_create(&port, &kernel) == Q2_OK);
    assert(q2_am178_controls(NULL, &control) == Q2_ERR_ARGUMENT);
    assert(q2_am178_controls(&panel, &control) == Q2_OK);
    /* Reproduce the old boot failure, then register the real corrected descriptor. */
    broken = control;
    broken.start = NULL;
    broken.stop = NULL;
    assert(q2_component_register(kernel, &broken) == Q2_ERR_ARGUMENT);
    assert(q2_component_register(kernel, &control) == Q2_OK);
    assert(q2_kernel_boot(kernel) == Q2_OK);
    q2_component_status_t status;
    assert(q2_component_status(kernel, 0, &status) == Q2_OK && status.state == Q2_READY);
    bool available = false;
    assert(q2_capability_available(kernel, "display.brightness", &available) == Q2_OK && available);
    q2_caller_t caller = {.instance = 1, .grants = {"display.brightness"}, .grant_count = 1};
    q2_value_t out, value = {.kind = Q2_INTEGER, .as.integer = 50};
    assert(q2_kernel_call(kernel, &caller, "panel", "brightness", NULL, 0, &out) == Q2_OK);
    assert(out.kind == Q2_INTEGER && out.as.integer == 30);
    assert(q2_kernel_call(kernel, &caller, "panel", "brightness", &value, 1, &out) == Q2_OK);
    assert(out.as.integer == 50 && panel.percent == 50);
    unsigned calls = panel.calls;
    value.as.integer = 101;
    assert(q2_kernel_call(kernel, &caller, "panel", "brightness", &value, 1, &out) == Q2_ERR_ARGUMENT);
    value.as.integer = -1;
    assert(q2_kernel_call(kernel, &caller, "panel", "brightness", &value, 1, &out) == Q2_ERR_ARGUMENT);
    value.kind = Q2_BOOLEAN;
    assert(q2_kernel_call(kernel, &caller, "panel", "brightness", &value, 1, &out) == Q2_ERR_ARGUMENT);
    caller.grant_count = 0;
    assert(q2_kernel_call(kernel, &caller, "panel", "brightness", NULL, 0, &out) == Q2_ERR_PERMISSION);
    assert(panel.calls == calls);
    caller.grant_count = 1;
    panel.fail = true;
    value.kind = Q2_INTEGER;
    value.as.integer = 20;
    assert(q2_kernel_call(kernel, &caller, "panel", "brightness", &value, 1, &out) == Q2_ERR_IO);
    assert(out.kind == Q2_NIL && panel.percent == 50);
    assert(q2_kernel_destroy(&kernel) == Q2_OK && !kernel);
    /* Pre-boot teardown also works when startup aborts before creating Lua. */
    assert(q2_kernel_create(&port, &kernel) == Q2_OK);
    assert(q2_component_register(kernel, &control) == Q2_OK);
    assert(q2_kernel_destroy(&kernel) == Q2_OK && !kernel);
    puts("PASS: real panel registration, boot, authorization, commands, I/O failure, teardown");
    return 0;
}
