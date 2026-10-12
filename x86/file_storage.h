#ifndef Q2_PC_FILE_STORAGE_H
#define Q2_PC_FILE_STORAGE_H
#include "q2/storage.h"
typedef struct q2_pc_storage q2_pc_storage_t;
/** Open an exclusively locked 256-KiB LittleFS image and register storage.private before boot.
 * Format only a newly created file; existing invalid/corrupt media is never reformatted. */
q2_result_t q2_pc_storage_open(q2_kernel_t *kernel, const char *path, q2_pc_storage_t **out_storage);
/** Close after Kernel destruction; NULL is accepted for uniform cleanup. */
q2_result_t q2_pc_storage_close(q2_pc_storage_t **storage);
#endif
