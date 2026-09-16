#pragma once
#include <atomic>
#include <string>
#include <vector>
#include <3ds.h>
#include "saves.h"
#include "snapshot_browse.h"

class SnapshotWorker {
public:
    SnapshotWorker(TitleInfo title);
    ~SnapshotWorker();

    void start_scan();
    void start_restore(size_t index);
    void join();

    SnapshotPhase phase() const;
    std::string status();
    std::vector<SnapshotEntry> snapshots();

private:
    TitleInfo title_;
    std::string key_dir_;
    std::atomic<int> phase_;
    std::atomic<bool> running_;
    LightLock mu_;
    char status_buf_[256];
    std::vector<SnapshotEntry> snapshots_;
    Thread thread_;

    static void scan_entry(void* arg);
    void scan_worker();

    struct RestoreCtx {
        SnapshotWorker* self;
        SnapshotEntry entry;
        size_t index;
    };
    RestoreCtx* pending_restore_;
    static void restore_entry(void* arg);
    void restore_worker(const SnapshotEntry& entry, size_t index);

    SnapshotWorker(const SnapshotWorker&);
    SnapshotWorker& operator=(const SnapshotWorker&);
};
