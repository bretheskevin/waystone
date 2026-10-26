/*
 * Preview stub for SnapshotsController.
 * start_scan() yields 3 canned snapshots in BrowsePhase::Ready; start_restore() succeeds at once.
 */
#include "snapshots_controller.h"
#include <cstdio>

SnapshotsController::SnapshotsController(TitleInfo title, AccountUid /*uid*/)
    : title_(std::move(title)), uid_{}
{}

SnapshotsController::~SnapshotsController() { join(); }

void SnapshotsController::start_scan() {
    std::lock_guard<std::mutex> lk(mu_);
    snapshots_.clear();
    // Fields: {timestamp, path, file_count, total_bytes}
    snapshots_.push_back({"20260915T143022Z", "/waystone/snapshots/a", 3, 1536ULL * 1024});
    snapshots_.push_back({"20260912T081210Z", "/waystone/snapshots/b", 3, 1490ULL * 1024});
    snapshots_.push_back({"20260901T190455Z", "/waystone/snapshots/c", 2, 820ULL * 1024});
    status_ = "3 snapshot(s) found";
    phase_.store(BrowsePhase::Ready);
}

void SnapshotsController::start_restore(size_t index) {
    printf("[snapshots-stub] restore requested for index %zu\n", index);
    { std::lock_guard<std::mutex> lk(mu_); status_ = "Restored! Safety backup saved."; }
    phase_.store(BrowsePhase::Done);
}

void SnapshotsController::join() {}

BrowsePhase SnapshotsController::phase() const { return phase_.load(); }

std::string SnapshotsController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<SnapshotEntry> SnapshotsController::snapshots() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snapshots_;
}
