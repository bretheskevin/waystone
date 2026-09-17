#include "snapshot_worker.h"
#include "worker_thread.h"
#include "snapshot.h"        // write_snapshot, snapshot_sanitize_key
#include "snapshot_browse.h" // list_snapshots, snapshot_to_flat_files_json, etc.
#include <cstdio>
#include <cstring>

SnapshotWorker::SnapshotWorker(TitleInfo title)
    : title_(title),
      phase_((int)BrowsePhase::Idle),
      running_(false),
      thread_(0),
      pending_restore_(0)
{
    key_dir_ = snapshot_key_dir("3ds", title_.name);
    LightLock_Init(&mu_);
    memset(status_buf_, 0, sizeof(status_buf_));
}

SnapshotWorker::~SnapshotWorker() {
    join();
    delete pending_restore_;
}

void SnapshotWorker::join() {
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = 0;
    }
}

BrowsePhase SnapshotWorker::phase() const {
    return (BrowsePhase)phase_.load();
}

std::string SnapshotWorker::status() {
    LightLock_Lock(&mu_);
    std::string s(status_buf_);
    LightLock_Unlock(&mu_);
    return s;
}

std::vector<SnapshotEntry> SnapshotWorker::snapshots() {
    LightLock_Lock(&mu_);
    std::vector<SnapshotEntry> copy = snapshots_;
    LightLock_Unlock(&mu_);
    return copy;
}

void SnapshotWorker::scan_entry(void* arg) {
    static_cast<SnapshotWorker*>(arg)->scan_worker();
}

void SnapshotWorker::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    join();
    phase_.store((int)BrowsePhase::Scanning);
    {
        LightLock_Lock(&mu_);
        snapshots_.clear();
        snprintf(status_buf_, sizeof(status_buf_), "Scanning snapshots...");
        LightLock_Unlock(&mu_);
    }
    thread_ = start_worker_thread(scan_entry, this);
    if (!thread_) {
        printf("[snapshot] threadCreate failed\n");
        phase_.store((int)BrowsePhase::Error);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
    }
}

void SnapshotWorker::scan_worker() {
    printf("[snapshot] scanning for %s (key_dir=%s)\n",
           title_.name.c_str(), key_dir_.c_str());

    std::vector<SnapshotEntry> entries =
        list_snapshots("sdmc:/waystone/backups", key_dir_.c_str());

    {
        LightLock_Lock(&mu_);
        snapshots_ = entries;
        snprintf(status_buf_, sizeof(status_buf_),
                 "%zu snapshot(s) found", entries.size());
        LightLock_Unlock(&mu_);
    }
    phase_.store(entries.empty() ? (int)BrowsePhase::Done
                                 : (int)BrowsePhase::Ready);
    running_.store(false);
}

void SnapshotWorker::start_restore(size_t index) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;

    SnapshotEntry entry;
    {
        LightLock_Lock(&mu_);
        if (index >= snapshots_.size()) {
            LightLock_Unlock(&mu_);
            running_.store(false);
            return;
        }
        entry = snapshots_[index];
        LightLock_Unlock(&mu_);
    }
    join();
    phase_.store((int)BrowsePhase::Restoring);
    {
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_),
                 "Restoring %s...", entry.timestamp.c_str());
        LightLock_Unlock(&mu_);
    }

    delete pending_restore_;
    pending_restore_ = new RestoreCtx();
    pending_restore_->self = this;
    pending_restore_->entry = entry;
    pending_restore_->index = index;
    thread_ = start_worker_thread(restore_entry, pending_restore_);
    if (!thread_) {
        phase_.store((int)BrowsePhase::Error);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
    }
}

void SnapshotWorker::restore_entry(void* arg) {
    RestoreCtx* ctx = static_cast<RestoreCtx*>(arg);
    ctx->self->restore_worker(ctx->entry, ctx->index);
    // ctx is owned by SnapshotWorker::pending_restore_ and freed in ~SnapshotWorker or next restore.
}

void SnapshotWorker::restore_worker(const SnapshotEntry& entry, size_t index) {
    (void)index;

    // 1. Always snapshot the CURRENT save first (unconditional guard)
    printf("[snapshot] guard: extracting current save for %s\n",
           title_.name.c_str());
    std::string raw_json = extract_save_json(title_);
    if (!raw_json.empty()) {
        std::string snap_ts = history_timestamp();
        std::string backup_dir = std::string("sdmc:/waystone/backups/") +
                                 key_dir_ + "/" + snap_ts;
        if (!write_snapshot(backup_dir.c_str(), raw_json.c_str())) {
            printf("[snapshot] guard snapshot FAILED, aborting restore\n");
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Restore failed: could not back up current save.");
            LightLock_Unlock(&mu_);
            phase_.store((int)BrowsePhase::Error);
            running_.store(false);
            return;
        }
        printf("[snapshot] guard snapshot saved to %s\n", backup_dir.c_str());
    }
    // If raw_json is empty, there is no current save to guard -- proceed.

    // 2. Restore each slot to its correct archive.
    // "main"    -> ARCHIVE_USER_SAVEDATA (SaveUser)
    // "extdata" -> ARCHIVE_EXTDATA       (SaveExtdata)
    // A slot absent from the snapshot ("[]") is skipped; only a genuine I/O
    // failure ("") aborts the restore.
    static const struct { const char* slot; SaveArchiveKind kind; } SLOTS[] = {
        { "main",    SaveUser    },
        { "extdata", SaveExtdata },
    };
    bool any_written = false;
    for (size_t si = 0; si < sizeof(SLOTS)/sizeof(SLOTS[0]); si++) {
        const char* slot = SLOTS[si].slot;
        SaveArchiveKind kind = SLOTS[si].kind;

        printf("[snapshot] reading snapshot %s slot=%s\n",
               entry.path.c_str(), slot);
        std::string flat = snapshot_to_flat_files_json(entry.path.c_str(), slot);
        if (flat.empty()) {
            printf("[snapshot] snapshot_to_flat_files_json FAILED slot=%s\n", slot);
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Restore failed: could not read snapshot.");
            LightLock_Unlock(&mu_);
            phase_.store((int)BrowsePhase::Error);
            running_.store(false);
            return;
        }
        if (flat == "[]") {
            printf("[snapshot] slot=%s has no files in snapshot, skipping\n", slot);
            continue;
        }

        printf("[snapshot] restoring slot=%s kind=%s files present\n",
               slot, (kind == SaveExtdata) ? "extdata" : "user");
        int wrc = write_save_files(title_.title_id, flat.c_str(), kind);
        if (wrc != 0) {
            printf("[snapshot] write_save_files FAILED slot=%s (rc=%d)\n", slot, wrc);
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Restore failed: write error.");
            LightLock_Unlock(&mu_);
            phase_.store((int)BrowsePhase::Error);
            running_.store(false);
            return;
        }
        any_written = true;
    }

    if (!any_written) {
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_),
                 "Snapshot is empty, nothing to restore.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Done);
        running_.store(false);
        return;
    }

    printf("[snapshot] restore complete\n");
    LightLock_Lock(&mu_);
    snprintf(status_buf_, sizeof(status_buf_),
             "Restored! Safety backup saved.");
    LightLock_Unlock(&mu_);
    phase_.store((int)BrowsePhase::Done);
    running_.store(false);
}
