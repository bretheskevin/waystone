#pragma once
#include <atomic>
#include <string>
#include <cstdint>
#include <vector>
#include <3ds.h>
#include "saves.h"
#include "session.h"
#include "history_browse.h"

class HistoryWorker {
public:
    HistoryWorker(Session* session, TitleInfo title);
    ~HistoryWorker();

    void start_scan();
    void start_restore(size_t index);
    void join();

    BrowsePhase phase() const;
    std::string status();
    std::vector<HistoryEntry> entries();

private:
    Session* session_;  // borrowed
    TitleInfo title_;
    std::atomic<int> phase_;
    std::atomic<bool> running_;
    LightLock mu_;
    char status_buf_[256];
    std::vector<HistoryEntry> entries_;
    Thread thread_;

    // Derived during scan, consumed by restore
    std::string base_path_;
    std::string group_key_;
    std::vector<uint8_t> raw_tree_;

    static void scan_entry(void* arg);
    void scan_worker();

    struct RestoreCtx {
        HistoryWorker* self;
        HistoryEntry entry;
        size_t index;
    };
    RestoreCtx* pending_restore_;
    static void restore_entry(void* arg);
    void restore_worker(const HistoryEntry& entry, size_t index);

    HistoryWorker(const HistoryWorker&);
    HistoryWorker& operator=(const HistoryWorker&);
};
