#ifndef WAYSTONE_FFI_ERR_H
#define WAYSTONE_FFI_ERR_H

struct Vault;
extern "C" {
#include "waystone.h"
}

// ws_last_error() for log lines; "unknown" when the FFI recorded none.
inline const char* ffi_err() {
    const char* e = ws_last_error();
    return e ? e : "unknown";
}

#endif
