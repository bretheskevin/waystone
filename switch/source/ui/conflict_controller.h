#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "net.h"
#include "saves.h"
#include "wsconfig.h"

struct Vault;
typedef Vault WsVault;

struct ConflictItem {
    uint32_t id = 0;            // stable, monotonically increasing
    bool queued = false;        // resolve waiting on the scan thread
    std::string title_name;
    TitleInfo title;
    std::string group_key, local_hash, local_mtime;
    std::string remote_hash, remote_device_id, remote_mtime;
    std::string base_path;
    std::vector<uint8_t> raw_tree;
};

// Display-only copy (no raw_tree) handed to the activity.
struct ConflictView {
    uint32_t id = 0;
    bool queued = false;
    std::string title_name, group_key, local_hash, local_mtime;
    std::string remote_hash, remote_device_id, remote_mtime;
};

enum class ConflictPhase { Idle, Scanning, Ready, Resolving, Done, Error };

class ConflictController {
public:
    ConflictController(WsVault* vault, AccountUid uid, std::string device_id, WebDavCfg dav,
                       std::vector<TitleInfo> titles, WaystoneShellConfig config);
    ~ConflictController();
    ConflictController(const ConflictController&) = delete;
    ConflictController& operator=(const ConflictController&) = delete;

    void start_scan();
    // Queued on the scan thread while a scan runs, otherwise run on a resolve thread.
    // Unknown (already resolved) or already-queued ids are ignored.
    void resolve_keep_local(uint32_t id)  { request_resolve(id, true); }
    void resolve_keep_remote(uint32_t id) { request_resolve(id, false); }
    void request_cancel() { cancel_.store(true); }
    bool is_running() const { return running_.load(); }
    void join();

    ConflictPhase phase() const;
    std::string status() const;
    uint32_t version() const { return version_.load(); }
    std::vector<ConflictView> views() const;

private:
    struct ResolveReq { uint32_t id; bool keep_local; };
    enum ResolveResult { RR_OK, RR_FAILED, RR_UNKNOWN };

    WsVault* vault_;
    AccountUid uid_;
    std::string device_id_;
    std::string dav_url_, dav_user_, dav_pass_;
    WebDavCfg dav_;
    std::vector<TitleInfo> titles_;
    WaystoneShellConfig config_;
    std::atomic<ConflictPhase> phase_{ConflictPhase::Idle};
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<uint32_t> version_{0};
    mutable std::mutex mu_;
    std::string status_;                   // guarded by mu_
    std::vector<ConflictItem> conflicts_;  // guarded by mu_
    std::vector<ResolveReq> queue_;        // guarded by mu_
    bool scan_active_ = false;             // guarded by mu_; true => resolves are queued
    uint32_t next_id_ = 1;                 // scan thread only
    std::thread thread_;

    void scan_worker();
    void drain_queue(WebDavSession* sess, int* resolved, int* failed);
    void request_resolve(uint32_t id, bool keep_local);
    void start_resolve(uint32_t id, bool keep_local);
    void resolve_worker(uint32_t id, bool keep_local);
    ResolveResult resolve_one(WebDavSession* sess, uint32_t id, bool keep_local);
    ConflictItem* find_locked(uint32_t id);  // mu_ must be held
};
