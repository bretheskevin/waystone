#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "saves.h"
#include "session.h"
#include "history_browse.h"

class HistoryController {
public:
    HistoryController(TitleInfo title, Session* session);
    ~HistoryController();
    HistoryController(const HistoryController&) = delete;
    HistoryController& operator=(const HistoryController&) = delete;

    void start_scan();
    void start_restore(size_t index);
    void join();

    BrowsePhase phase() const;
    std::string status() const;
    std::vector<HistoryEntry> entries() const;
    const TitleInfo& title() const { return title_; }

private:
    TitleInfo title_;
    Session* session_;  // borrowed, not owned
    std::atomic<BrowsePhase> phase_{BrowsePhase::Idle};
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    std::string status_;
    std::vector<HistoryEntry> entries_;
    std::thread thread_;

    // Derived during scan, consumed by restore
    std::string base_path_;
    std::string group_key_;
    std::string raw_json_;

    void scan_worker();
    void restore_worker(HistoryEntry entry, size_t index);
};
