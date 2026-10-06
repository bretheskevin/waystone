#include "history_worker.h"
#include "worker_thread.h"
#include "sync.h"           // resolve_save_locations, restore_remote_save, SaveLocation
#include "history_browse.h" // list_history
#include "snapshot_browse.h" // human_timestamp
#include <cstdio>
#include <cstring>

HistoryWorker::HistoryWorker(Session* session, TitleInfo title)
    : session_(session),
      title_(title),
      phase_((int)BrowsePhase::Idle),
      running_(false),
      thread_(0),
      pending_restore_(0)
{
    LightLock_Init(&mu_);
    memset(status_buf_, 0, sizeof(status_buf_));
}

HistoryWorker::~HistoryWorker() {
    join();
    delete pending_restore_;
}

void HistoryWorker::join() {
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = 0;
    }
}

BrowsePhase HistoryWorker::phase() const {
    return (BrowsePhase)phase_.load();
}

std::string HistoryWorker::status() {
    LightLock_Lock(&mu_);
    std::string s(status_buf_);
    LightLock_Unlock(&mu_);
    return s;
}

std::vector<HistoryEntry> HistoryWorker::entries() {
    LightLock_Lock(&mu_);
    std::vector<HistoryEntry> copy = entries_;
    LightLock_Unlock(&mu_);
    return copy;
}

void HistoryWorker::scan_entry(void* arg) {
    static_cast<HistoryWorker*>(arg)->scan_worker();
}

void HistoryWorker::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    join();
    phase_.store((int)BrowsePhase::Scanning);
    {
        LightLock_Lock(&mu_);
        entries_.clear();
        snprintf(status_buf_, sizeof(status_buf_), "Scanning history...");
        LightLock_Unlock(&mu_);
    }
    thread_ = start_worker_thread(scan_entry, this);
    if (!thread_) {
        printf("[history] threadCreate failed\n");
        phase_.store((int)BrowsePhase::Error);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
    }
}

void HistoryWorker::scan_worker() {
    printf("[history] scanning for %s\n", title_.name.c_str());

    bool had_error = false;
    std::vector<SaveLocation> locations =
        resolve_save_locations(session_->vault, title_, &had_error);

    if (had_error) {
        printf("[history] normalize failed for %s\n", title_.name.c_str());
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Normalize failed.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Error);
        running_.store(false);
        return;
    }

    if (locations.empty() || locations[0].base_path.empty()) {
        printf("[history] no save location for %s\n", title_.name.c_str());
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "No local save found.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Done);
        running_.store(false);
        return;
    }

    base_path_ = locations[0].base_path;
    group_key_ = locations[0].group_key;
    raw_tree_  = locations[0].raw_tree;

    WebDavCfg dav = session_->dav.as_cfg();

    printf("[history] listing %s/history/\n", base_path_.c_str());
    LightLock_Lock(&mu_);
    snprintf(status_buf_, sizeof(status_buf_), "Fetching history...");
    LightLock_Unlock(&mu_);

    std::vector<HistoryEntry> results = list_history(session_->vault, base_path_, dav);

    {
        LightLock_Lock(&mu_);
        entries_ = results;
        snprintf(status_buf_, sizeof(status_buf_),
                 "%zu history version(s) found", results.size());
        LightLock_Unlock(&mu_);
    }
    phase_.store(results.empty() ? (int)BrowsePhase::Done
                                 : (int)BrowsePhase::Ready);
    running_.store(false);
}

void HistoryWorker::start_restore(size_t index) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;

    HistoryEntry entry;
    {
        LightLock_Lock(&mu_);
        if (index >= entries_.size()) {
            LightLock_Unlock(&mu_);
            running_.store(false);
            return;
        }
        entry = entries_[index];
        LightLock_Unlock(&mu_);
    }
    join();
    phase_.store((int)BrowsePhase::Restoring);
    {
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_),
                 "Restoring %s...", human_timestamp(entry.timestamp).c_str());
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

void HistoryWorker::restore_entry(void* arg) {
    RestoreCtx* ctx = static_cast<RestoreCtx*>(arg);
    ctx->self->restore_worker(ctx->entry, ctx->index);
}

void HistoryWorker::restore_worker(const HistoryEntry& entry, size_t index) {
    (void)index;
    WebDavCfg dav = session_->dav.as_cfg();

    // Hash is resolved lazily (list_history only did a single PROPFIND), so fetch
    // this one entry's head now to learn which blob to restore.
    std::string hash = fetch_history_hash(session_->vault, entry, dav);
    if (hash.empty()) {
        printf("[history] could not resolve hash for %s\n", title_.name.c_str());
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Could not read version.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Error);
        running_.store(false);
        return;
    }

    printf("[history] restoring hash=%.12s for %s\n", hash.c_str(), title_.name.c_str());

    WebDavSession* sess = webdav_session_begin(dav);
    if (!sess) {
        printf("[history] webdav_session_begin failed\n");
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Network init failed.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Error);
        running_.store(false);
        return;
    }
    int rc = restore_remote_save(session_->vault, hash,
                                 base_path_, group_key_, raw_tree_,
                                 title_, sess);
    webdav_session_end(sess);
    if (rc != 0) {
        printf("[history] restore FAILED (rc=%d)\n", rc);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Restore failed.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Error);
        running_.store(false);
        return;
    }

    printf("[history] restore complete\n");
    LightLock_Lock(&mu_);
    snprintf(status_buf_, sizeof(status_buf_), "Restored! Safety backup saved.");
    LightLock_Unlock(&mu_);
    phase_.store((int)BrowsePhase::Done);
    running_.store(false);
}
