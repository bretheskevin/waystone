#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "saves.h"
#include "snapshot_browse.h"

class SnapshotsController {
public:
    SnapshotsController(TitleInfo title, AccountUid uid);
    ~SnapshotsController();  // joins: only deleted via reap_worker once !is_running()
    SnapshotsController(const SnapshotsController&) = delete;
    SnapshotsController& operator=(const SnapshotsController&) = delete;

    void start_scan();
    void start_restore(size_t index);
    // No-op: the scan is one local listing and a restore must never stop mid-write.
    void request_cancel() {}
    bool is_running() const { return running_.load(); }
    void join();

    BrowsePhase phase() const;
    std::string status() const;
    std::vector<SnapshotEntry> snapshots() const;
    const TitleInfo& title() const { return title_; }

private:
    TitleInfo title_;
    AccountUid uid_;
    std::string key_dir_;
    std::atomic<BrowsePhase> phase_{BrowsePhase::Idle};
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    std::string status_;
    std::vector<SnapshotEntry> snapshots_;
    std::thread thread_;

    void scan_worker();
    void restore_worker(SnapshotEntry entry, size_t index);
};
