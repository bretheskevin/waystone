#ifndef WAYSTONE_3DS_SYNC_H
#define WAYSTONE_3DS_SYNC_H

#include "net.h"
#include "saves.h"
#include <cstdint>
#include <string>
#include <vector>

struct Vault;
typedef Vault WsVault;

// Optional progress sink. All callbacks run on the calling (worker) thread.
struct SyncProgress {
    void (*step)(void* ctx, const char* label);          // may be null
    bool (*bytes)(size_t got, size_t total, void* ctx);  // may be null; return true to continue
    void* ctx;
};

struct PushStats {
    int uploaded;   // blobs actually PUT (excludes blobs already on the server)
    int failed;     // saves that hit a failure path and were skipped
};

struct PullStats {
    int pulled;
    int conflicts;         // conflict_needs_input decisions (not resolved here)
    int restore_failures;  // restore_remote_save != 0, or no pull hash
    int scan_failures;     // scan_save_decision produced no decision (server/decrypt error)
};

// Push all saves for a single title to the WebDAV backend.
// 3DS savedata is per-title (no AccountUid).
// Returns number of saves pushed (0 = nothing to push, -1 = error).
// stats/prog are optional; stats is zeroed on entry when provided.
int push_title(const WsVault* vault,
               const TitleInfo& title,
               const char* device_id,
               WebDavSession* dav,
               PushStats* stats = nullptr,
               SyncProgress* prog = nullptr);

// Pull all saves for a single title from the WebDAV backend (NewestWins, non-interactive).
// 3DS savedata is per-title (no AccountUid).
// Returns number of saves pulled (0 = nothing to pull, -1 = error).
// stats/prog are optional; stats is zeroed on entry when provided.
int pull_title(const WsVault* vault,
               const TitleInfo& title,
               const char* device_id,
               WebDavSession* dav,
               PullStats* stats = nullptr,
               SyncProgress* prog = nullptr);

// Result of scanning one save to determine the sync decision.
struct SaveDecision {
    std::string decision_type;  // "in_sync","push","pull","conflict_resolved","conflict_needs_input"
    std::string pull_hash;      // hash to pull (empty if no pull needed)
    std::string group_key;
    std::string local_hash;
    std::string base_path;      // obfuscated remote base path
    std::string heads_array;    // raw JSON array of decrypted DeviceHead objects
    std::vector<uint8_t> raw_tree; // raw extracted local WsFileTree (for snapshot/restore)
    std::string winner;         // "local" or "remote" for conflict_resolved
};

// Scan one normalized save entry to determine what sync action is needed.
// save_meta: a single save's metadata JSON string (mtime already injected).
// files_ptr/files_len: that save's inline WsFileTree (aliases the normalize buffer).
// policy: 0 = NewestWins, 1 = Prompt. On failure, decision_type is empty.
SaveDecision scan_save_decision(const WsVault* vault, const char* save_meta,
                                const char* mtime, const char* device_id,
                                int policy, WebDavSession* dav,
                                const std::vector<uint8_t>& raw_tree,
                                const uint8_t* files_ptr, size_t files_len);

// Restore a remote save blob to the local filesystem.
// Safety snapshot taken before write. No AccountUid (3DS).
// The title's is_twl flag routes the write to the correct archive.
// Returns 0 on success, -1 on error.
int restore_remote_save(const WsVault* vault, const std::string& pull_hash,
                        const std::string& base_path, const std::string& group_key,
                        const std::vector<uint8_t>& raw_tree, const TitleInfo& title,
                        WebDavSession* dav, SyncProgress* prog = 0);

// Scan all saves for a title: extract -> normalize("3ds") -> save_list_decode -> per-save scan_save_decision.
// Returns decisions for each normalized save. Empty vector = no local saves or nothing to scan.
// If error is non-null and a normalize failure occurs, *error is set to true.
std::vector<SaveDecision> scan_title(const WsVault* vault, const TitleInfo& title,
                                     const char* device_id, int policy,
                                     WebDavSession* dav, bool* error = 0,
                                     SyncProgress* prog = 0);

// Lightweight result of resolving a save's remote location from local data only.
// No network I/O — unlike scan_title, skips the per-save heads PROPFIND/GET/decrypt.
// Used by the History browse view to open the remote history listing.
struct SaveLocation {
    std::string base_path;  // obfuscated remote base path (empty if make_base_path failed)
    std::string group_key;
    std::vector<uint8_t> raw_tree; // raw extracted local WsFileTree (same for all entries; needed for restore)
};

// Resolve the remote base_path/group_key for a title's saves using LOCAL data only:
// extract_save_json -> ws_checkpoint_normalize -> save_list_decode -> per save:
// ws_package -> group_key -> make_base_path. NO network.
// Empty vector = no local saves. If a normalize failure occurs and error is non-null,
// *error is set to true.
std::vector<SaveLocation> resolve_save_locations(const WsVault* vault,
                                                 const TitleInfo& title,
                                                 bool* error = 0);

#endif
