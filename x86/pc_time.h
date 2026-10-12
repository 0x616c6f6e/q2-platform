#ifndef Q2_PC_TIME_H
#define Q2_PC_TIME_H
#include <stdbool.h>
#include <stdint.h>
/** Monotonic milliseconds for owner-thread scheduling; independent of wall-clock adjustments. */
uint64_t q2_pc_now_ms(void);
/** Sleep between owner polls; interrupted POSIX sleeps resume for the remaining duration. */
void q2_pc_sleep_ms(unsigned milliseconds);
/** Read local civil time. Returns false when the OS cannot supply a valid timestamp. */
bool q2_pc_local_time(unsigned *out_hour, unsigned *out_minute);
#endif
