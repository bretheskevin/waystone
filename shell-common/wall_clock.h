#ifndef WAYSTONE_WALL_CLOCK_H
#define WAYSTONE_WALL_CLOCK_H

#include <sys/time.h>

// Wall-clock milliseconds (gettimeofday) for the shells' "... in N ms" logs.
inline unsigned long long wall_now_ms() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)(tv.tv_usec / 1000);
}

inline unsigned long long wall_ms_since(unsigned long long t0) { return wall_now_ms() - t0; }

#endif
