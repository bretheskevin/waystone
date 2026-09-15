#ifndef WAYSTONE_3DS_SYNC_H
#define WAYSTONE_3DS_SYNC_H

#include "net.h"
#include "saves.h"
#include <string>
#include <vector>

struct Vault;
typedef Vault WsVault;

// Push all saves for a single title to the WebDAV backend.
// 3DS savedata is per-title (no AccountUid).
// Returns number of saves pushed (0 = nothing to push, -1 = error).
int push_title(const WsVault* vault,
               const TitleInfo& title,
               const char* device_id,
               const WebDavCfg& dav);

// Pull all saves for a single title from the WebDAV backend (NewestWins, non-interactive).
// 3DS savedata is per-title (no AccountUid).
// Returns number of saves pulled (0 = nothing to pull, -1 = error).
int pull_title(const WsVault* vault,
               const TitleInfo& title,
               const char* device_id,
               const WebDavCfg& dav);

// Result of scanning one save to determine the sync decision.
struct SaveDecision {
    std::string decision_type;  // "in_sync","push","pull","conflict_resolved","conflict_needs_input"
    std::string pull_hash;      // hash to pull (empty if no pull needed)
    std::string group_key;
    std::string local_hash;
    std::string base_path;      // obfuscated remote base path
    std::string heads_array;    // raw JSON array of decrypted DeviceHead objects
    std::string raw_json;       // raw extracted local save JSON (for snapshot/restore)
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
// Safety snapshot taken before write. No AccountUid (3DS).
// Returns 0 on success, -1 on error.
int restore_remote_save(const WsVault* vault, const std::string& pull_hash,
                        const std::string& base_path, const std::string& group_key,
                        const std::string& raw_json, u64 title_id,
                        const WebDavCfg& dav);

// Scan all saves for a title: extract -> normalize("3ds") -> split -> per-save scan_save_decision.
// Returns decisions for each normalized save. Empty vector = no local saves or nothing to scan.
// If error is non-null and a normalize failure occurs, *error is set to true.
std::vector<SaveDecision> scan_title(const WsVault* vault, const TitleInfo& title,
                                     const char* device_id, int policy,
                                     const WebDavCfg& dav, bool* error = 0);

#endif
