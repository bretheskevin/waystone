#pragma once
#include <atomic>
#include <string>
#include <vector>
#include <3ds.h>
#include "net.h"
#include "saves.h"

struct Vault;
typedef Vault WsVault;

struct ConflictItem {
    std::string title_name;
    u64 title_id;
    u32 unique_id;              // needed by push_title -> extract_save_json
    std::string group_key;      // "system/game/slot"
    std::string local_hash;
    std::string local_mtime;
    std::string remote_hash;
    std::string remote_device_id;
    std::string remote_mtime;
    std::string base_path;      // obfuscated remote path
    std::string heads_array;    // raw JSON array of decrypted heads
    std::string raw_json;       // raw extracted local save (for snapshot/restore)
};

enum class ConflictPhase { Idle, Scanning, Ready, Resolving, Done, Error };

class ConflictWorker {
public:
    ConflictWorker(WsVault* vault, const std::string& device_id,
                   const WebDavCfg& dav, std::vector<TitleInfo> titles);
    ~ConflictWorker();

    void start_scan();
    void resolve_keep_local(size_t index) { start_resolve(index, true); }
    void resolve_keep_remote(size_t index) { start_resolve(index, false); }
    void join();
    // Ask the scan loop to stop before the next title so a screen pop's join()
    // doesn't block the render thread for the whole title list.
    void request_cancel() { cancel_.store(true); }
    // True while the worker thread body is still executing. Lets a detached
    // worker be reaped (deleted) only once its thread has finished, so join()
    // in the destructor returns immediately instead of blocking the caller.
    bool is_running() const { return running_.load(); }

    ConflictPhase phase() const;            // atomic load
    std::string status();                   // LightLock-guarded copy
    std::vector<ConflictItem> conflicts();  // LightLock-guarded copy

private:
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
    LightLock mu_;
    char status_buf_[256];
    std::vector<ConflictItem> conflicts_;
    Thread thread_;

    static void scan_entry(void* arg);
    static void resolve_entry(void* arg);
    void scan_worker();

    struct ResolveCtx {
        ConflictWorker* self;
        bool keep_local;
        ConflictItem item;
        size_t index;
    };
    ResolveCtx* pending_resolve_;
    void start_resolve(size_t index, bool keep_local);
    void resolve_worker(bool keep_local, ConflictItem item, size_t index);

    ConflictWorker(const ConflictWorker&);
    ConflictWorker& operator=(const ConflictWorker&);
};
