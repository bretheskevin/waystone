#pragma once
#include <atomic>
#include <string>
#include <vector>
#include <3ds.h>
#include "net.h"
#include "saves.h"

struct Vault;
typedef Vault WsVault;

enum class SyncPhase { Idle, Running, Done, Error };

class SyncWorker {
public:
    SyncWorker(WsVault* vault, const std::string& device_id, const WebDavCfg& dav,
               std::vector<TitleInfo> titles);
    ~SyncWorker();

    void start();               // idempotent: no-op if already running
    void join();
    SyncPhase phase() const;    // atomic load
    std::string status();       // LightLock-guarded snapshot
    int pushed_count() const;
    int restored_count() const;
    int total_count() const;
    const std::vector<TitleInfo>& titles() const;

private:
    WsVault* vault_;
    std::string device_id_;
    std::string dav_url_;
    std::string dav_user_;
    std::string dav_pass_;
    WebDavCfg dav_;
    std::vector<TitleInfo> titles_;
    std::atomic<int> phase_;
    std::atomic<int> pushed_;
    std::atomic<int> restored_;
    std::atomic<bool> running_;
    LightLock mu_;
    char status_buf_[256];
    Thread thread_;

    static void thread_entry(void* arg);
    void worker();

    SyncWorker(const SyncWorker&);
    SyncWorker& operator=(const SyncWorker&);
};
