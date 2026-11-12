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

// ws_keys_mode on a keys.json (NULL ok): false when unrecognized, else *plain_out is set.
inline bool ffi_keys_mode(const uint8_t* keys, size_t len, bool* plain_out) {
    int32_t mode = keys ? ws_keys_mode(keys, len) : -1;
    if (mode != WS_KEYS_MODE_PLAIN && mode != WS_KEYS_MODE_ENCRYPTED) return false;
    *plain_out = mode == WS_KEYS_MODE_PLAIN;
    return true;
}

// true only for a recognized unencrypted keys.json.
inline bool ffi_keys_plain(const uint8_t* keys, size_t len) {
    bool plain = false;
    return ffi_keys_mode(keys, len, &plain) && plain;
}

// Log label of a keys.json mode.
inline const char* keys_mode_name(bool plain) { return plain ? "plain" : "encrypted"; }

#endif
