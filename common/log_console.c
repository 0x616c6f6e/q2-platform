#include "log_console.h"
#include <stdio.h>
#include <string.h>
q2_result_t q2_console_log_parse(const char *text, q2_log_level_t *out) {
    if (!text || !out)
        return Q2_ERR_ARGUMENT;
    const char *names[] = {"off", "error", "warn", "info", "debug"};
    for (unsigned i = 0; i <= Q2_LOG_DEBUG; i++)
        if (!strcmp(text, names[i])) {
            *out = (q2_log_level_t)i;
            return Q2_OK;
        }
    return Q2_ERR_ARGUMENT;
}
#if Q2_ENABLE_LOG
static void one_line(char *out, const char *text, size_t capacity) {
    size_t i = 0;
    for (; i + 1 < capacity && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        out[i] = c < 32 || c == 127 ? ' ' : (char)c;
    }
    out[i] = 0;
}
static q2_result_t write_record(void *ctx, const q2_log_record_t *record) {
    (void)ctx;
    const char *names[] = {"OFF", "ERROR", "WARN", "INFO", "DEBUG"};
    char tag[32], message[Q2_LOG_MESSAGE_MAX];
    /* Keep untrusted Lua error/message text from injecting new terminal lines or escape controls. */
    one_line(tag, record->tag, sizeof(tag));
    one_line(message, record->message, sizeof(message));
    int n = fprintf(stderr, "[%llu ms] [%-5s] [%s] %s%s\n", (unsigned long long)record->timestamp_ms,
                    names[record->level], tag, message, record->truncated ? " [truncated]" : "");
    return n < 0 ? Q2_ERR_IO : Q2_OK;
}
#endif
q2_result_t q2_console_log_attach(q2_kernel_t *kernel, q2_log_level_t level) {
    if (!kernel || level < Q2_LOG_OFF || level > Q2_LOG_DEBUG)
        return Q2_ERR_ARGUMENT;
#if Q2_ENABLE_LOG
    const q2_log_sink_t sink = {Q2_LOG_ABI, sizeof(sink), NULL, write_record};
    return q2_log_configure(kernel, &sink, level);
#else
    return Q2_OK;
#endif
}
