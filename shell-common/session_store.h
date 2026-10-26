#ifndef WAYSTONE_SESSION_STORE_H
#define WAYSTONE_SESSION_STORE_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include "secure_clear.h"

// Platform-specific device key accessor.
// Switch: splGetConfig(SplConfigItem_DeviceId) -> 8-byte hardware ID.
// 3DS: cfguGetConfigInfoBlk2(0x00090001) -> unique console ID.
// Set once at startup before calling session_store_save/load.
typedef bool (*DeviceKeyFn)(uint8_t* out_key, size_t* out_len);
void session_store_set_device_key_fn(DeviceKeyFn fn);

// Path: sdmc:/waystone/session.bin
static constexpr const char* SESSION_STORE_PATH = "sdmc:/waystone/session.bin";

// Save MDK + WebDAV password to session.bin, encrypted with the device key.
// mdk must be exactly 32 bytes. Returns true on success.
bool session_store_save(const uint8_t* mdk, size_t mdk_len,
                        const std::string& webdav_pass);

// Load MDK + WebDAV password from session.bin.
// On success: writes 32 bytes to mdk_out, populates pass_out, returns true.
// On failure (missing file, corrupt blob, wrong device): returns false.
// All output buffers are zeroized on failure.
bool session_store_load(uint8_t* mdk_out, size_t mdk_cap,
                        std::string& pass_out);

// Returns true if session.bin exists and is non-empty.
bool session_store_exists();

// Delete session.bin. Zeroizes the file content before unlinking.
void session_store_clear();

// Forward declaration for FFI type (full definition via waystone.h).
extern "C" { struct Vault; }
typedef Vault WsVault;

// Export the MDK from vault and persist it to session.bin for auto-unlock. false on failure.
bool persist_session(WsVault* vault, const std::string& webdav_pass);

// Settings "WebDAV password" row text: masked, never the secret itself.
inline std::string webdav_pass_label(const std::string& pass) {
    return std::string("WebDAV password: ") +
           (pass.empty() ? "not set" : "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2");
}

// Settings "WebDAV password" edit: replaces `current` in place (old bytes wiped) with `entered`,
// re-persists session.bin when a vault is unlocked, then wipes `entered`. Empty input changes
// nothing. Returns true when the password changed.
inline bool update_webdav_pass(WsVault* vault, std::string& current, std::string& entered) {
    if (entered.empty()) {
        printf("[vault] webdav password unchanged (empty input)\n");
        return false;
    }
    if (!current.empty()) secure_clear(&current[0], current.size());
    current = entered;
    secure_clear(&entered[0], entered.size());
    entered.clear();
    bool persisted = vault && persist_session(vault, current);
    printf("[vault] webdav password updated (persisted=%d)\n", persisted ? 1 : 0);
    return true;
}

// Load the persisted session and reconstruct the runtime vault in one step.
// On success: returns a WsVault* (caller owns → ws_vault_free) and fills pass_out
// with the WebDAV password. On failure: returns nullptr and clears pass_out.
// The 32-byte MDK is zeroized internally on every path.
WsVault* session_store_load_vault(std::string& pass_out);

#endif
