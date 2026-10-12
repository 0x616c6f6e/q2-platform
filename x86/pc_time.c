#define _POSIX_C_SOURCE 200809L
#include "pc_time.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif
uint64_t q2_pc_now_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0;
    return (uint64_t)value.tv_sec * 1000 + (uint64_t)value.tv_nsec / 1000000;
#endif
}
void q2_pc_sleep_ms(unsigned milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec delay = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
#endif
}
bool q2_pc_local_time(unsigned *out_hour, unsigned *out_minute) {
    if (!out_hour || !out_minute)
        return false;
#ifdef _WIN32
    SYSTEMTIME local;
    GetLocalTime(&local);
    *out_hour = local.wHour;
    *out_minute = local.wMinute;
#else
    time_t now = time(NULL);
    struct tm local;
    if (now == (time_t)-1 || !localtime_r(&now, &local))
        return false;
    *out_hour = (unsigned)local.tm_hour;
    *out_minute = (unsigned)local.tm_min;
#endif
    return true;
}
