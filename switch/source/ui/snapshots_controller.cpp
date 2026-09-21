#include "snapshots_controller.h"
#include "snapshot.h"        // write_snapshot, snapshot_sanitize_key
#include "snapshot_browse.h" // list_snapshots, snapshot_to_flat_files_json, etc.
#include <cstdio>

SnapshotsController::SnapshotsController(TitleInfo title, AccountUid uid)
    : title_(std::move(title)), uid_(uid) {
    key_dir_ = snapshot_key_dir("switch", title_.name);
}

SnapshotsController::~SnapshotsController() { join(); }

void SnapshotsController::join() {
    if (thread_.joinable()) thread_.join();
}

BrowsePhase SnapshotsController::phase() const { return phase_.load(); }

std::string SnapshotsController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<SnapshotEntry> SnapshotsController::snapshots() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snapshots_;
}

void SnapshotsController::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    if (thread_.joinable()) thread_.join();
    phase_.store(BrowsePhase::Scanning);
    { std::lock_guard<std::mutex> lk(mu_);
      status_ = "Scanning snapshots...";
      snapshots_.clear(); }
    thread_ = std::thread(&SnapshotsController::scan_worker, this);
}

void SnapshotsController::scan_worker() {
    printf("[snapshot] scanning for %s (key_dir=%s)\n",
           title_.name.c_str(), key_dir_.c_str());

    std::vector<SnapshotEntry> entries =
        list_snapshots("sdmc:/waystone/backups", key_dir_.c_str());

    {
        std::lock_guard<std::mutex> lk(mu_);
        snapshots_ = entries;
        char buf[128];
        snprintf(buf, sizeof(buf), "%zu snapshot(s) found", entries.size());
        status_ = buf;
    }
    phase_.store(entries.empty() ? BrowsePhase::Done : BrowsePhase::Ready);
    running_.store(false);
}

void SnapshotsController::start_restore(size_t index) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    SnapshotEntry entry;
    { std::lock_guard<std::mutex> lk(mu_);
      if (index >= snapshots_.size()) { running_.store(false); return; }
      entry = snapshots_[index]; }
    if (thread_.joinable()) thread_.join();
    phase_.store(BrowsePhase::Restoring);
    { std::lock_guard<std::mutex> lk(mu_);
      status_ = "Restoring " + entry.timestamp + "..."; }
    thread_ = std::thread(&SnapshotsController::restore_worker, this,
                          std::move(entry), index);
}

void SnapshotsController::restore_worker(SnapshotEntry entry, size_t index) {
    (void)index;

    // 1. Always snapshot the CURRENT save first (unconditional guard)
    printf("[snapshot] guard: extracting current save for %s\n",
           title_.name.c_str());
    std::string raw_json = extract_save_json(title_, uid_);
    if (!raw_json.empty()) {
        std::string snap_ts = history_timestamp();
        std::string backup_dir = std::string("sdmc:/waystone/backups/") +
                                 key_dir_ + "/" + snap_ts;
        if (!write_snapshot(backup_dir.c_str(), raw_json.c_str())) {
            printf("[snapshot] guard snapshot FAILED, aborting restore\n");
            { std::lock_guard<std::mutex> lk(mu_);
              status_ = "Restore failed: could not back up current save."; }
            phase_.store(BrowsePhase::Error);
            running_.store(false);
            return;
        }
        printf("[snapshot] guard snapshot saved to %s\n", backup_dir.c_str());
        std::string backup_root = std::string("sdmc:/waystone/backups/") + key_dir_;
        if (!snapshot_prune(backup_root.c_str(), SNAPSHOT_KEEP)) {
            printf("[snapshot] prune failed for %s (non-fatal)\n", key_dir_.c_str());
        }
    }

    // 2. Read the selected snapshot into a flat files_json
    printf("[snapshot] reading snapshot %s\n", entry.path.c_str());
    std::string flat_json = snapshot_to_flat_files_json(entry.path.c_str());
    if (flat_json.empty()) {
        { std::lock_guard<std::mutex> lk(mu_);
          status_ = "Restore failed: could not read snapshot."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }
    if (flat_json == "[]") {
        { std::lock_guard<std::mutex> lk(mu_);
          status_ = "Snapshot is empty, nothing to restore."; }
        phase_.store(BrowsePhase::Done);
        running_.store(false);
        return;
    }

    // 3. Write the snapshot files to the save mount
    printf("[snapshot] writing save files for title %016lX\n", title_.title_id);
    int wrc = write_save_files(title_.title_id, uid_, flat_json.c_str());
    if (wrc != 0) {
        printf("[snapshot] write_save_files FAILED (rc=%d)\n", wrc);
        { std::lock_guard<std::mutex> lk(mu_);
          status_ = "Restore failed: write error."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }

    printf("[snapshot] restore complete\n");
    { std::lock_guard<std::mutex> lk(mu_);
      status_ = "Restored! Safety backup saved."; }
    phase_.store(BrowsePhase::Done);
    running_.store(false);
}
