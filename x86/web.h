#ifndef Q2_PC_WEB_H
#define Q2_PC_WEB_H
#include "device_data.h"
#include "q2/keys.h"
#include "q2/lvgl.h"
typedef struct q2_pc_web q2_pc_web_t;
/** Start the optional loopback-only simulator HTTP server; port 0 chooses a free port.
 * Caller retains providers and optional device_data; NULL disables device simulation. All socket work and
 * input forwarding run on the existing owner thread. */
q2_result_t q2_pc_web_create(q2_kernel_t *kernel, q2_keys_t *keys, q2_lvgl_t *display,
                             q2_pc_device_data_t *device_data, unsigned port, q2_pc_web_t **out_web);
/** Advance bounded nonblocking connections. Client errors are HTTP errors, not Kernel failures. */
q2_result_t q2_pc_web_poll(q2_pc_web_t *web);
/** Close sockets and free snapshots before provider destruction; accepts a NULL server. */
q2_result_t q2_pc_web_destroy(q2_pc_web_t **web);
#endif
