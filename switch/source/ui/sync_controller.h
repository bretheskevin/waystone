#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "net.h"
#include "saves.h"

// WsVault is an opaque Rust FFI type; only used as a pointer here.
struct Vault;
typedef Vault WsVault;

enum class SyncPhase { Idle, Running, Done, Error };

class SyncController {
public:
    SyncController(WsVault* vault, AccountUid uid, std::string device_id,
                   WebDavCfg dav, std::vector<TitleInfo> titles);
    ~SyncController();
    SyncController(const SyncController&) = delete;
    SyncController& operator=(const SyncController&) = delete;

    void start();                 // idempotent: no-op if already running
    void join();
    SyncPhase phase() const;      // atomic load
    std::string status() const;   // mutex-guarded snapshot, thread-safe
    int pushed_count() const;
    int restored_count() const;
    const std::vector<TitleInfo>& titles() const;  // immutable after construction

private:
    WsVault* vault_;
    AccountUid uid_;
    std::string device_id_;
    WebDavCfg dav_;
    std::vector<TitleInfo> titles_;
    std::atomic<SyncPhase> phase_{SyncPhase::Idle};
    std::atomic<bool> running_{false};
    std::atomic<int> pushed_{0};
    std::atomic<int> restored_{0};
    mutable std::mutex mu_;
    std::string status_;
    std::thread thread_;

    void worker();
};
