#ifndef WAYSTONE_VAULT_REMOTE_H
#define WAYSTONE_VAULT_REMOTE_H

#include "net.h"
#include "wsconfig.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Vault;
extern "C" {
#include "waystone.h"
}

// The server's /keys.json is the single source of truth for the vault (desktop/Android parity):
// setup creates it only when absent; every other device joins by unlocking it.

// Injectable GET/PUT so the logic is host-testable. Both return the HTTP status, or -1 on a
// transport error. get fills *out only on 2xx. put must never overwrite (If-None-Match: *).
struct VaultDav {
    int (*get)(void* ctx, const char* path, std::vector<uint8_t>* out);
    int (*put)(void* ctx, const char* path, const uint8_t* data, size_t len);
    void* ctx;
    std::string log_url; // already redacted (webdav_loggable_url)
};

// Wraps webdav_get/webdav_put; `dav` must outlive the returned value.
VaultDav vault_dav_from_cfg(const WebDavCfg& dav);

enum class RemoteKeysState { Absent, Present, Error };

struct RemoteKeys {
    RemoteKeysState state;
    std::vector<uint8_t> bytes;
    int http_status; // -1 = transport error
};

// GET /keys.json: 404 -> Absent; 2xx non-empty -> Present; anything else -> Error.
RemoteKeys vault_fetch_remote_keys(const VaultDav& dav);
RemoteKeys vault_fetch_remote_keys(const WebDavCfg& dav);

struct VaultSetupResult {
    WsVault* vault;              // non-null on success; caller owns (ws_vault_free)
    std::string recovery_hex;    // create only; wipe with vault_setup_wipe_recovery when done
    std::vector<uint8_t> keys_json;
    std::string error;           // non-empty on failure
};

// Creates a vault, re-probes the server right before publishing (refuses when a vault already
// exists or the probe fails) and PUTs /keys.json. Any refusal or failed PUT frees the vault and
// returns an error.
VaultSetupResult vault_create_and_publish(const VaultDav& dav, const std::string& passphrase);
VaultSetupResult vault_create_and_publish(const WebDavCfg& dav, const std::string& passphrase);

// Unlocks the server's keys.json with the passphrase (or the recovery key when `recovery`).
VaultSetupResult vault_join(const std::vector<uint8_t>& server_keys, const std::string& secret,
                            bool recovery);

// Writes keys.json (creating its parent dir) and, when recovery_hex is set, the recovery file.
bool vault_persist_local(const VaultSetupResult& r, const char* keys_path,
                         const std::string& recovery_path);

// After a successful create/join (worker thread): vault_persist_local + config.json. Failures
// are only logged: the server copy stays the source of truth (a later setup re-joins it) and a
// created vault's recovery key must still reach the recovery screen.
void vault_setup_persist(const VaultSetupResult& r, const char* keys_path,
                         const std::string& recovery_path, const WaystoneShellConfig& cfg,
                         const char* config_path);

// Zeroizes and clears r->recovery_hex.
void vault_setup_wipe_recovery(VaultSetupResult* r);

// Legacy local-only vaults: upload local keys.json when the server has none. Never overwrites;
// a different server vault only logs a warning.
void vault_publish_if_absent(const VaultDav& dav, const uint8_t* local_keys, size_t len);
void vault_publish_if_absent(const WebDavCfg& dav, const uint8_t* local_keys, size_t len);

// Post-unlock hook (worker thread): vault_publish_if_absent with the keys.json at keys_path.
void vault_publish_local_if_absent(const VaultDav& dav, const char* keys_path);
void vault_publish_local_if_absent(const WebDavCfg& dav, const char* keys_path);

#endif
