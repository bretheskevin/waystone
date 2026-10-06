#ifndef WAYSTONE_SYNC_H
#define WAYSTONE_SYNC_H

#include "net.h"
#include "saves.h"

#include <cstdint>
#include <string>
#include <vector>

struct Vault;
typedef Vault WsVault;

// Push all saves for a single title to the WebDAV backend.
// Steps per save:
//   1. ws_jksv_normalize -> binary WsSaveList (per save: metadata + file tree)
//   2. For each: set mtime, ws_package -> SaveEntry + zip
//   3. ws_vault_encrypt_blob -> encrypted blob
//   4. ws_vault_blob_name -> obfuscated blob name
//   5. ws_vault_path_segment -> obfuscated path segments
//   6. webdav_mkdir_p_s + webdav_put_s (session) (blob, head, history)
// Returns number of saves pushed (0 means nothing to push, -1 means error).
int push_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               WebDavSession* dav);

// Pull/restore saves for a single title from the WebDAV backend.
// Returns number of saves restored (0 = nothing to do, -1 = error).
int pull_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               WebDavSession* dav);

// Result of scanning one save to determine the sync decision.
struct SaveDecision {
    std::string decision_type;  // "in_sync","push","pull","conflict_resolved","conflict_needs_input"
    std::string pull_hash;      // hash to pull (empty if no pull needed)
    std::string group_key;
    std::string local_hash;
    std::string base_path;      // obfuscated remote base path
    std::string heads_array;    // raw JSON array of decrypted heads
    std::vector<uint8_t> raw_tree; // raw extracted local WsFileTree (for snapshot)
    std::string winner;         // "local" or "remote" for conflict_resolved
    std::string mtime;          // UTC timestamp used when packaging this save
};

// Scan one normalized save entry to determine what sync action is needed.
// save_meta: a single save's metadata JSON (mtime already injected).
// files_ptr/files_len: that save's WsFileTree slice (for ws_package).
// policy: 0 = NewestWins, 1 = Prompt. On failure, decision_type is empty.
SaveDecision scan_save_decision(const WsVault* vault, const char* save_meta,
                                const char* mtime, const char* device_id,
                                int policy, WebDavSession* dav,
                                const std::vector<uint8_t>& raw_tree,
                                const uint8_t* files_ptr, size_t files_len);

// Restore a remote save blob to the local filesystem.
// Returns 0 on success, -1 on error.
int restore_remote_save(const WsVault* vault, const std::string& pull_hash,
                        const std::string& base_path, const std::string& group_key,
                        const std::vector<uint8_t>& raw_tree, u64 title_id,
                        AccountUid uid, WebDavSession* dav);

// Scan all saves for a title: extract -> ws_jksv_normalize("switch") -> save_list_decode
// -> per-save scan_save_decision. Returns decisions for each normalized save.
// Empty vector = no local saves or nothing to scan.
// If error is non-null and a normalize failure occurs, *error is set to true.
std::vector<SaveDecision> scan_title(const WsVault* vault, const TitleInfo& title,
                                     AccountUid uid,
                                     const char* device_id, int policy,
                                     WebDavSession* dav, bool* error = nullptr);

#endif
