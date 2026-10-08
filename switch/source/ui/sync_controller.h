#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "net.h"
#include "saves.h"
#include "sync_summary.h"
#include "wsconfig.h"

// WsVault is an opaque Rust FFI type; only used as a pointer here.
struct Vault;
typedef Vault WsVault;

enum class SyncPhase { Idle, Running, Done, Error };

class SyncController {
public:
    // config: borrowed (Session-owned); conflict_policy/safety_backup are read at start().
    SyncController(WsVault* vault, AccountUid uid, std::string device_id,
                   WebDavCfg dav, std::vector<TitleInfo> titles,
                   const WaystoneShellConfig* config);
    ~SyncController();
    SyncController(const SyncController&) = delete;
    SyncController& operator=(const SyncController&) = delete;

    void start();                 // idempotent: no-op if already running
    void join();
    SyncPhase phase() const;      // atomic load
    std::string status() const;   // "Syncing i/n: name" while running, headline when done
    std::vector<TitleResult> results() const;  // one per title, mutex-guarded snapshot
    const std::vector<TitleInfo>& titles() const;  // immutable after construction

    std::string step() const;      // current step label, "" between titles
    int current_index() const;     // -1 when none
    size_t bytes_got() const;
    size_t bytes_total() const;
    float progress() const;        // 0..1 single pass
    int total_count() const;

private:
    WsVault* vault_;
    AccountUid uid_;
    std::string device_id_;
    std::string dav_url_;
    std::string dav_user_;
    std::string dav_pass_;
    WebDavCfg dav_;
    std::vector<TitleInfo> titles_;
    const WaystoneShellConfig* config_;
    int policy_ = 0;
    bool safety_backup_ = true;
    std::atomic<SyncPhase> phase_{SyncPhase::Idle};
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    std::string status_;
    std::vector<TitleResult> results_;
    std::thread thread_;
    std::atomic<int> cur_index_{-1};
    std::atomic<size_t> xfer_got_{0};
    std::atomic<size_t> xfer_total_{0};
    std::string step_;  // guarded by mu_

    static void on_step(void* ctx, const char* label);
    static bool on_bytes(size_t got, size_t total, void* ctx);
    void worker();
    void set_result(size_t i, TitleState s, const std::string& reason);
};
