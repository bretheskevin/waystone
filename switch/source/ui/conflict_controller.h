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
    std::string title_name;
    uint64_t title_id;
    AccountUid uid;
    std::string group_key;
    std::string local_hash;
    std::string local_mtime;
    std::string remote_hash;
    std::string remote_device_id;
    std::string remote_mtime;
    std::string base_path;
    std::string heads_array;
    std::vector<uint8_t> raw_tree;
};

enum class ConflictPhase { Idle, Scanning, Ready, Resolving, Done, Error };

class ConflictController {
public:
    ConflictController(WsVault* vault, AccountUid uid, std::string device_id,
                       WebDavCfg dav, std::vector<TitleInfo> titles,
                       WaystoneShellConfig config);
    ~ConflictController();
    ConflictController(const ConflictController&) = delete;
    ConflictController& operator=(const ConflictController&) = delete;

    void start_scan();
    void resolve_keep_local(size_t index);
    void resolve_keep_remote(size_t index);
    void join();

    ConflictPhase phase() const;
    std::string status() const;
    std::vector<ConflictItem> conflicts() const;

private:
    WsVault* vault_;
    AccountUid uid_;
    std::string device_id_;
    std::string dav_url_;
    std::string dav_user_;
    std::string dav_pass_;
    WebDavCfg dav_;
    std::vector<TitleInfo> titles_;
    WaystoneShellConfig config_;
    std::atomic<ConflictPhase> phase_{ConflictPhase::Idle};
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    std::string status_;
    std::vector<ConflictItem> conflicts_;
    std::thread thread_;
    void scan_worker();
    void resolve_impl(bool keep_local, size_t index);
    void resolve_worker(bool keep_local, ConflictItem item, size_t index);
};
