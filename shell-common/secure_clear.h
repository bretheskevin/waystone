#ifndef WAYSTONE_SECURE_CLEAR_H
#define WAYSTONE_SECURE_CLEAR_H

#include <cstddef>

// Non-elidable zeroing via volatile writes — prevents the compiler from
// optimizing away the loop when the buffer goes out of scope immediately after.
inline void secure_clear(void* p, size_t n) {
    volatile char* v = reinterpret_cast<volatile char*>(p);
    for (size_t i = 0; i < n; i++) v[i] = '\0';
}

#endif
