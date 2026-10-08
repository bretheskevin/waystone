#pragma once
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>
#include <3ds.h>
#include "net.h"
#include "saves.h"
#include "sync_summary.h"
#include "sync_engine.h"
#include "wsconfig.h"

struct Vault;
typedef Vault WsVault;

enum class SyncPhase { Idle, Running, Done, Error };

class SyncWorker {
public:
    SyncWorker(WsVault* vault, const std::string& device_id, const WebDavCfg& dav,
               std::vector<TitleInfo> titles, const WaystoneShellConfig& config);
    ~SyncWorker();

    void start();               // idempotent: no-op if already running
    void join();
    SyncPhase phase() const;    // atomic load
    std::string status();       // LightLock-guarded snapshot (final headline once Done)
    std::string step();         // LightLock-guarded current step label ("" between titles)
    std::vector<TitleResult> results();  // LightLock-guarded copy, one entry per title
    int current_index() const;  // active title index, -1 when none
    size_t bytes_got() const;
    size_t bytes_total() const;
    float progress() const;     // single pass 0..1
    int total_count() const;
    const std::vector<TitleInfo>& titles() const;

private:
    WsVault* vault_;
    std::string device_id_;
    std::string dav_url_;
    std::string dav_user_;
    std::string dav_pass_;
    WebDavCfg dav_;
    WaystoneShellConfig config_;
    std::vector<TitleInfo> titles_;
    std::atomic<int> phase_;
    std::atomic<int> cur_index_;
    std::atomic<size_t> xfer_got_;
    std::atomic<size_t> xfer_total_;
    std::atomic<bool> running_;
    LightLock mu_;
    char status_buf_[256];               // guarded by mu_
    char step_buf_[48];                  // guarded by mu_
    std::vector<TitleResult> results_;   // guarded by mu_
    Thread thread_;

    static void thread_entry(void* arg);
    static void on_step(void* ctx, const char* label);
    static bool on_bytes(size_t got, size_t total, void* ctx);
    void worker();
    void begin_title(size_t i);
    void set_result(size_t i, TitleState state, const std::string& reason);

    SyncWorker(const SyncWorker&);
    SyncWorker& operator=(const SyncWorker&);
};
