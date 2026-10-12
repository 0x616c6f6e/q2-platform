#ifndef Q2_PC_NETWORK_H
#define Q2_PC_NETWORK_H
#include "q2/network.h"
typedef struct q2_pc_network q2_pc_network_t;
/** Create the native HTTP transport (WinHTTP on Windows; libcurl/pthreads on Linux).
 * Owner-thread calls only. Workers copy inputs and never call Kernel/Lua.
 * Returns MEMORY/IO/UNSUPPORTED on initialization failure; out remains NULL.
 * Linux requires libcurl with asynchronous DNS and uses system CA certificates. */
q2_result_t q2_pc_network_create(q2_pc_network_t **out_transport, q2_network_backend_t *out_backend);
/** Cancel and reclaim workers after network component destruction; NULL is accepted.
 * Teardown may wait up to 11 seconds. BUSY retains the backend for a later retry;
 * workers are never forcefully terminated. Ordinary request release never waits. */
q2_result_t q2_pc_network_destroy(q2_pc_network_t **transport);
#endif
