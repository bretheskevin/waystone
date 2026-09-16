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
    ~SnapshotsController();
    SnapshotsController(const SnapshotsController&) = delete;
    SnapshotsController& operator=(const SnapshotsController&) = delete;

    void start_scan();
    void start_restore(size_t index);
    void join();

    SnapshotPhase phase() const;
    std::string status() const;
    std::vector<SnapshotEntry> snapshots() const;
    const TitleInfo& title() const { return title_; }

private:
    TitleInfo title_;
    AccountUid uid_;
    std::string key_dir_;
    std::atomic<SnapshotPhase> phase_{SnapshotPhase::Idle};
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    std::string status_;
    std::vector<SnapshotEntry> snapshots_;
    std::thread thread_;

    void scan_worker();
    void restore_worker(SnapshotEntry entry, size_t index);
};
