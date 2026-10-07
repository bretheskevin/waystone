#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <3ds.h>
#include "net.h"
#include "saves.h"

struct Vault;
typedef Vault WsVault;

struct ConflictItem {
    u32 id;                     // stable, monotonically increasing; 0 = none
    bool queued;                // resolve request waiting on the scan thread
    std::string title_name;
    TitleInfo title;            // full title (icon cleared): ROM sources need rom/save paths for push/restore
    std::string group_key;      // "system/game/slot"
    std::string local_hash;
    std::string local_mtime;
    std::string remote_hash;
    std::string remote_device_id;
    std::string remote_mtime;
    std::string base_path;      // obfuscated remote path
    std::string heads_array;    // raw JSON array of decrypted heads
    std::vector<uint8_t> raw_tree; // raw extracted local save tree (for snapshot/restore)
};

// Display-only copy handed to the screen: no raw_tree / heads_array.
struct ConflictView {
    u32 id;
    bool queued;
    std::string title_name;
    std::string group_key;
    std::string local_hash;
    std::string remote_hash;
    std::string remote_device_id;
    std::string remote_mtime;
};

enum class ConflictPhase { Idle, Scanning, Ready, Resolving, Done, Error };

class ConflictWorker {
public:
    ConflictWorker(WsVault* vault, const std::string& device_id,
                   const WebDavCfg& dav, std::vector<TitleInfo> titles);
    ~ConflictWorker();

    void start_scan();
    // Queued on the scan thread while a scan runs, otherwise run on a resolve thread.
    // Unknown (already resolved) or already-queued ids are ignored.
    void resolve_keep_local(u32 id)  { request_resolve(id, true); }
    void resolve_keep_remote(u32 id) { request_resolve(id, false); }
    void join();
    // Ask the scan loop to stop before the next title (and stop draining queued
    // resolves) so a screen pop's join() doesn't block the render thread.
    void request_cancel() { cancel_.store(true); }
    // True while the worker thread body is still executing. Lets a detached
    // worker be reaped (deleted) only once its thread has finished, so join()
    // in the destructor returns immediately instead of blocking the caller.
    bool is_running() const { return running_.load(); }

    ConflictPhase phase() const;            // atomic load
    std::string status();                   // LightLock-guarded copy
    u32 version() const { return version_.load(); } // bumped on every conflicts_ change
    std::vector<ConflictView> views();      // LightLock-guarded lightweight copy

private:
    struct ResolveReq {
        u32 id;
        bool keep_local;
    };
    struct ResolveCtx {
        ConflictWorker* self;
        bool keep_local;
        u32 id;
    };
    enum ResolveResult { RR_OK, RR_FAILED, RR_UNKNOWN };

    WsVault* vault_;
    std::string device_id_;
    std::string dav_url_;
    std::string dav_user_;
    std::string dav_pass_;
    WebDavCfg dav_;
    std::vector<TitleInfo> titles_;
    std::atomic<int> phase_;
    std::atomic<bool> running_;
    std::atomic<bool> cancel_;
    std::atomic<u32> version_;
    LightLock mu_;
    char status_buf_[256];
    std::vector<ConflictItem> conflicts_;   // guarded by mu_
    std::vector<ResolveReq> queue_;          // guarded by mu_
    bool scan_active_;                       // guarded by mu_; true => resolves are queued
    u32 next_id_;                            // scan thread only
    Thread thread_;
    ResolveCtx* pending_resolve_;

    static void scan_entry(void* arg);
    static void resolve_entry(void* arg);
    void scan_worker();
    void drain_queue(WebDavSession* sess, int* resolved, int* failed);

    void request_resolve(u32 id, bool keep_local);
    void start_resolve(u32 id, bool keep_local);
    void resolve_worker(u32 id, bool keep_local);
    ResolveResult resolve_one(WebDavSession* sess, u32 id, bool keep_local);

    ConflictItem* find_locked(u32 id);       // mu_ must be held

    ConflictWorker(const ConflictWorker&);
    ConflictWorker& operator=(const ConflictWorker&);
};
