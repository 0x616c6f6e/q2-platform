/* Portable registration glue; hardware access remains in am178.c. */
#include "am178.h"

static q2_result_t controls_start(q2_kernel_t *kernel, void *context) {
    (void)kernel;
    /* The owner initializes hardware before registration. Do not reset a live panel here. */
    return context ? Q2_OK : Q2_ERR_ARGUMENT;
}
static q2_result_t controls_stop(q2_kernel_t *kernel, void *context) {
    (void)kernel;
    (void)context;
    /* Borrowed panel: the owner releases it after Kernel and LVGL teardown. */
    return Q2_OK;
}
static q2_result_t brightness_call(q2_kernel_t *kernel, void *context, const q2_caller_t *caller,
                                    const q2_value_t *args, size_t count, q2_value_t *out) {
    (void)kernel;
    (void)caller;
    if (count > 1 || (count == 1 && (!args || args[0].kind != Q2_INTEGER ||
                                   args[0].as.integer < 0 || args[0].as.integer > 100)))
        return Q2_ERR_ARGUMENT;
    unsigned percent = count ? (unsigned)args[0].as.integer : 0;
    q2_result_t e = q2_am178_brightness(context, count == 1, &percent);
    if (e == Q2_OK)
        *out = (q2_value_t){.kind = Q2_INTEGER, .as.integer = percent};
    return e;
}
static const q2_method_t panel_methods[] = {
    {"panel", "brightness", "display.brightness", brightness_call}
};
q2_result_t q2_am178_controls(q2_am178_t *s, q2_component_t *out) {
    if (!s || !out)
        return Q2_ERR_ARGUMENT;
    *out = (q2_component_t){.abi = Q2_COMPONENT_ABI, .size = sizeof(*out),
        .id = "panel_control", .required = true, .context = s,
        .start = controls_start, .stop = controls_stop,
        .methods = panel_methods, .method_count = sizeof(panel_methods) / sizeof(panel_methods[0])};
    return Q2_OK;
}
