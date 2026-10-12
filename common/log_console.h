#ifndef Q2_CONSOLE_LOG_H
#define Q2_CONSOLE_LOG_H
#include "q2/log.h"
/** Parse off/error/warn/info/debug; output is valid only on success. */
q2_result_t q2_console_log_parse(const char *text, q2_log_level_t *out_level);
/** Attach an owner-thread stderr logger. In compiled-out builds this is a successful no-op. */
q2_result_t q2_console_log_attach(q2_kernel_t *kernel, q2_log_level_t level);
#endif
