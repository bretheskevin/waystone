#ifndef WAYSTONE_SYNC_H
#define WAYSTONE_SYNC_H

#include "net.h"
#include "saves.h"

#include <string>

struct Vault;
typedef Vault WsVault;

// Push all saves for a single title to the WebDAV backend.
// Steps per save:
//   1. ws_jksv_normalize -> array of NormalizedSaveDto
//   2. For each: set mtime, ws_package -> SaveEntry + zip
//   3. ws_vault_encrypt_blob -> encrypted blob
//   4. ws_vault_blob_name -> obfuscated blob name
//   5. ws_vault_path_segment -> obfuscated path segments
//   6. webdav_mkdir_p + webdav_put (blob, head, history)
// Returns number of saves pushed (0 means nothing to push, -1 means error).
int push_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               const WebDavCfg& dav);

// Pull/restore saves for a single title from the WebDAV backend.
// Mirrors desktop do_pull_save pipeline, per normalized local save:
//   1. Package local save -> local_hash + group_key -> base_path
//   2. PROPFIND {base_path}/heads -> decrypt each .json -> heads array
//   3. ws_decide_pull -> determine pull_hash (or skip)
//   4. If pull: GET+decrypt blob -> ws_unzip -> write_save_files
// Returns number of saves restored (0 = nothing to do, -1 = error).
int pull_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               const WebDavCfg& dav);

// Result of scanning one save to determine the sync decision.
struct SaveDecision {
    std::string decision_type;  // "in_sync","push","pull","conflict_resolved","conflict_needs_input"
    std::string pull_hash;      // hash to pull (empty if no pull needed)
    std::string group_key;
    std::string local_hash;
    std::string base_path;      // obfuscated remote base path
    std::string heads_array;    // raw JSON array of decrypted heads
    std::string raw_json;       // raw extracted local save JSON (for snapshot)
    std::string winner;         // "local" or "remote" for conflict_resolved
};

// Scan one normalized save entry to determine what sync action is needed.
// save_json: a single NormalizedSaveDto JSON string (mtime already injected).
// policy: 0 = NewestWins, 1 = Prompt. On failure, decision_type is empty.
SaveDecision scan_save_decision(const WsVault* vault, const char* save_json,
                                const char* mtime, const char* device_id,
                                int policy, const WebDavCfg& dav,
                                const std::string& raw_json);

// Restore a remote save blob to the local filesystem.
// Returns 0 on success, -1 on error.
int restore_remote_save(const WsVault* vault, const std::string& pull_hash,
                        const std::string& base_path, const std::string& group_key,
                        const std::string& raw_json, u64 title_id,
                        AccountUid uid, const WebDavCfg& dav);

#endif
