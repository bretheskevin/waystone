/*
 * Preview stub for switch/source/ui/history_controller.{h,cpp}.
 * Provides 3 canned history entries in BrowsePhase::Ready state.
 * Linked INSTEAD of the real history_controller.cpp in the preview build.
 */
#include "history_controller.h"
#include <cstdio>

HistoryController::HistoryController(TitleInfo title, Session* session)
    : title_(std::move(title)), session_(session) {}

HistoryController::~HistoryController() { join(); }

void HistoryController::join() {
    if (thread_.joinable()) thread_.join();
}

BrowsePhase HistoryController::phase() const { return phase_.load(); }

std::string HistoryController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<HistoryEntry> HistoryController::entries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return entries_;
}

void HistoryController::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    phase_.store(BrowsePhase::Scanning);
    {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.clear();
        // Fields: {timestamp, device_id, get_path, hash} — hash stays empty until
        // resolved lazily at restore, mirroring the real list_history.
        entries_.push_back({"20260915T143022Z", "switch-abc1", "", ""});
        entries_.push_back({"20260914T091500Z", "3ds-def2",    "", ""});
        entries_.push_back({"20260913T200045Z", "switch-abc1", "", ""});
        status_ = "3 history version(s) found";
    }
    phase_.store(BrowsePhase::Ready);
    running_.store(false);
}

void HistoryController::start_restore(size_t index) {
    (void)index;
    printf("[history-stub] restore requested for index %zu\n", index);
    { std::lock_guard<std::mutex> lk(mu_); status_ = "Restored! Safety backup saved."; }
    phase_.store(BrowsePhase::Done);
}

void HistoryController::scan_worker() {}
void HistoryController::restore_worker(HistoryEntry, size_t) {}
