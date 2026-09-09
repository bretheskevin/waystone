/*
 * Preview stubs for all Waystone Rust FFI symbols (from ffi/include/waystone.h).
 * unlock_activity.cpp includes waystone.h directly and calls
 * ws_vault_unlock_pass / ws_vault_unlock_recovery / ws_last_error.
 * All other symbols are stubbed for completeness to satisfy the linker.
 */
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Define Vault as a complete type before waystone.h typedef-aliases it.
struct Vault { int _pad; };

extern "C" {
#include "waystone.h"
}

// ---------------------------------------------------------------------------
// Buffer / string lifecycle
// ---------------------------------------------------------------------------
void ws_buf_free(WsBuf buf)    { free(buf.ptr); }
void ws_string_free(char* s)   { free(s); }

// ---------------------------------------------------------------------------
// Vault lifecycle
// ---------------------------------------------------------------------------
static Vault g_stub_vault;

WsVault* ws_vault_init(const char* /*passphrase*/,
                       WsBuf*      out_recovery_hex,
                       WsBuf*      out_keys_json)
{
    if (out_recovery_hex) { *out_recovery_hex = {(uint8_t*)strdup("PREVIEW"), 7}; }
    if (out_keys_json)    { *out_keys_json    = {(uint8_t*)strdup("{}"),     2}; }
    return &g_stub_vault;
}

WsVault* ws_vault_unlock_pass(const char* /*passphrase*/,
                               const uint8_t* /*keys*/, uintptr_t /*n*/)
{
    return &g_stub_vault;
}

WsVault* ws_vault_unlock_recovery(const char* /*recovery_hex*/,
                                   const uint8_t* /*keys*/, uintptr_t /*n*/)
{
    return &g_stub_vault;
}

void ws_vault_free(WsVault* /*vault*/) {}

// ---------------------------------------------------------------------------
// Error
// ---------------------------------------------------------------------------
const char* ws_last_error(void) { return "preview stub — no real error"; }

// ---------------------------------------------------------------------------
// Encryption / decryption (no-op stubs returning empty buffers)
// ---------------------------------------------------------------------------
WsBuf ws_vault_encrypt_blob(const WsVault*, const uint8_t*, uintptr_t) { return {nullptr, 0}; }
WsBuf ws_vault_decrypt_blob(const WsVault*, const uint8_t*, uintptr_t) { return {nullptr, 0}; }
WsBuf ws_vault_encrypt_heads(const WsVault*, const uint8_t*, uintptr_t) { return {nullptr, 0}; }
WsBuf ws_vault_decrypt_heads(const WsVault*, const uint8_t*, uintptr_t) { return {nullptr, 0}; }
char* ws_vault_blob_name(const WsVault*, const char*) { return strdup("preview-blob"); }
char* ws_vault_path_segment(const WsVault*, const char*) { return strdup("preview-seg"); }

// ---------------------------------------------------------------------------
// Save / normalise stubs
// ---------------------------------------------------------------------------
WsBuf ws_canonical_zip(const char*)                                            { return {nullptr, 0}; }
char* ws_unzip(const uint8_t*, uintptr_t)                                      { return strdup("[]"); }
char* ws_content_hash(const uint8_t*, uintptr_t)                               { return strdup("preview-hash"); }
char* ws_file_hash(const uint8_t*, uintptr_t)                                  { return strdup("preview-hash"); }
char* ws_package(const char*, WsBuf*)                                          { return strdup("{}"); }
char* ws_fold_heads(const char*)                                               { return strdup("{}"); }
char* ws_three_way_sync(const char*, const char*, const char*,
                         const char*, const char*, int)                        { return strdup("{}"); }
char* ws_decide_pull(const char*, const char*, const char*, const char*, int)  { return strdup("{}"); }

// Format-normalise stubs
char* ws_checkpoint_normalize(const char*, const char*) { return strdup("{}"); }
char* ws_checkpoint_to_native(const char*)              { return strdup("[]"); }
char* ws_jksv_normalize(const char*, const char*)       { return strdup("{}"); }
char* ws_jksv_to_native(const char*)                    { return strdup("[]"); }
char* ws_mgba_normalize(const char*, const char*)       { return strdup("{}"); }
char* ws_mgba_to_native(const char*)                    { return strdup("[]"); }
char* ws_twilight_normalize(const char*)                { return strdup("{}"); }
char* ws_twilight_to_native(const char*)                { return strdup("[]"); }
