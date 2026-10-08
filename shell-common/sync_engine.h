#ifndef WAYSTONE_SYNC_ENGINE_H
#define WAYSTONE_SYNC_ENGINE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "net.h"
#include "sync_summary.h"
#include "wsconfig.h"

struct Vault;
typedef Vault WsVault;

// Optional progress sink. All callbacks run on the calling (worker) thread.
struct SyncProgress {
    void (*step)(void* ctx, const char* label);          // may be null
    bool (*bytes)(size_t got, size_t total, void* ctx);  // may be null; return true to continue
    void* ctx;
};

struct SyncEngineCfg {
    int conflict_policy;    // 0 = NewestWins, 1 = Prompt (ws_decide_pull int)
    bool safety_backup;     // snapshot the local save before every restore
    const char* device_id;  // this device's head file name
};

SyncEngineCfg sync_cfg_from(const WaystoneShellConfig& cfg, const char* device_id);

// One normalized local save. files_ptr aliases the LocalSaveSet's owned save-list buffer.
struct LocalSave {
    std::string meta_json;    // normalized metadata, "mtime":"" not yet injected
    const uint8_t* files_ptr;
    size_t files_len;
    std::string local_mtime;  // ISO-8601 UTC ("YYYY-MM-DDTHH:MM:SSZ") or "" when unknown
    LocalSave() : files_ptr(0), files_len(0) {}
};

// Everything a shell reports for one title. Owns the FFI save-list buffer (ws_buf_free once).
class LocalSaveSet {
public:
    std::vector<uint8_t> raw_tree;  // whole extracted local tree (safety-snapshot source)
    std::vector<LocalSave> saves;
    LocalSaveSet();
    ~LocalSaveSet();
    // Take ownership of a ws_*_normalize save list (ptr/len of its WsBuf), decode it into
    // `saves` (each stamped with `local_mtime`) and move `raw` into raw_tree. Call at most once.
    // false = decode failed: the buffer is freed here and the set is left empty.
    bool adopt_normalized(uint8_t* ptr, size_t len, std::vector<uint8_t>& raw,
                          const std::string& local_mtime);
private:
    uint8_t* savelist_ptr_;
    size_t savelist_len_;
    LocalSaveSet(const LocalSaveSet&);
    LocalSaveSet& operator=(const LocalSaveSet&);
};

// Per-shell hooks. `title` is the shell's own TitleInfo (opaque to the engine).
struct ShellOps {
    // Fill `out` with the title's local saves. 0 = ok (zero saves allowed), nonzero = failure.
    int (*list_saves)(void* ctx, const void* title, LocalSaveSet& out);
    // Optional (may be null): group_keys that exist only on the server (3DS ROM slots).
    int (*list_remote_only)(void* ctx, const WsVault* vault, WebDavSession* dav,
                            const void* title,
                            const std::vector<std::string>& local_group_keys,
                            std::vector<std::string>& out_group_keys);
    // Write a decrypted, unzipped WsFileTree for `group_key` back to the device. 0 = ok.
    int (*write_save)(void* ctx, const void* title, const std::string& group_key,
                      const uint8_t* tree, size_t tree_len);
    void* ctx;
};

struct SaveDecision {
    std::string decision_type;  // in_sync|push|pull|conflict_resolved|conflict_needs_input|"" (failed)
    std::string pull_hash;      // hash to restore (pull / conflict_resolved remote)
    std::string head_hash;      // merged (newest) head hash (ws_fold_heads); "" when no heads
    std::string head_device_id; // device that wrote the merged head
    std::string head_mtime;     // merged (newest) head mtime; becomes our base mtime after a pull
    std::string own_head_hash;  // this device's head hash on the server ("" = none)
    std::string group_key;
    std::string local_hash;     // "" for remote-only saves
    std::string local_mtime;
    std::string base_path;      // obfuscated remote base path
    std::string heads_array;    // decrypted heads JSON array
    std::string winner;         // "local"/"remote" for conflict_resolved
    int save_index;             // index into LocalSaveSet::saves; -1 = remote-only
    std::vector<uint8_t> raw_tree;  // filled only by sync_scan_title
    SaveDecision() : save_index(-1) {}
};

// "system/game/slot" -> obfuscated "<sys>/<game>/<slot>"; "" on failure.
std::string sync_base_path(const WsVault* vault, const std::string& group_key);

// Decide every save of the title first, then act. Never aborts: failures land in the tally.
TitleTally sync_title(const WsVault* vault, const void* title, const char* title_name,
                      const ShellOps& ops, const SyncEngineCfg& cfg, WebDavSession* dav,
                      const SyncProgress* prog);

// Decisions only (no action), each carrying a copy of the raw tree. Used by Conflicts/History.
std::vector<SaveDecision> sync_scan_title(const WsVault* vault, const void* title,
                                          const char* title_name, const ShellOps& ops,
                                          const SyncEngineCfg& cfg, int policy,
                                          WebDavSession* dav, bool* error,
                                          const SyncProgress* prog);

// Keep-local: push exactly the local save whose group_key matches. 0 = ok.
int sync_push_group(const WsVault* vault, const void* title, const char* title_name,
                    const ShellOps& ops, const SyncEngineCfg& cfg,
                    const std::string& group_key, WebDavSession* dav, const SyncProgress* prog);

struct LocalSaveKey {
    std::string group_key;
    std::string base_path;  // "" when the vault could not derive it
};

// Local-only (no network): package every local save of `title` to learn its group_key/base_path.
// Saves whose packaging fails are skipped. `raw_tree` (optional) receives the extracted local tree.
// Returns the number of local saves listed (>= 0; 0 = no local save), or -1 if list_saves failed.
int sync_local_keys(const WsVault* vault, const void* title, const ShellOps& ops,
                    std::vector<LocalSaveKey>& out, std::vector<uint8_t>* raw_tree);

// Restore `hash` WITHOUT touching this device's head (history restore). 0 = ok.
int sync_restore_hash(const WsVault* vault, const void* title, const ShellOps& ops,
                      const SyncEngineCfg& cfg, const std::string& base_path,
                      const std::string& group_key, const std::string& hash,
                      const std::vector<uint8_t>& raw_tree, WebDavSession* dav,
                      const SyncProgress* prog);

// Pull / keep-remote: restore `hash` then PUT heads/<device_id>.json = {hash, head_mtime}. 0 = ok.
int sync_pull_hash(const WsVault* vault, const void* title, const ShellOps& ops,
                   const SyncEngineCfg& cfg, const std::string& base_path,
                   const std::string& group_key, const std::string& hash,
                   const std::string& head_mtime, const std::vector<uint8_t>& raw_tree,
                   WebDavSession* dav, const SyncProgress* prog);

#endif
