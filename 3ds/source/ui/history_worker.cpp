#include "history_worker.h"
#include "worker_thread.h"
#include "sync.h"           // scan_title, restore_remote_save, SaveDecision
#include "history_browse.h" // list_history
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

    WebDavCfg dav = session_->dav.as_cfg();
    bool had_error = false;
    std::vector<SaveDecision> decisions =
        scan_title(session_->vault, title_, session_->device_id.c_str(),
                   0 /* NewestWins */, dav, &had_error);

    if (had_error) {
        printf("[history] normalize failed for %s\n", title_.name.c_str());
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Normalize failed.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Error);
        running_.store(false);
        return;
    }

    if (decisions.empty() || decisions[0].base_path.empty()) {
        printf("[history] no save decision for %s\n", title_.name.c_str());
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "No local save found.");
        LightLock_Unlock(&mu_);
        phase_.store((int)BrowsePhase::Done);
        running_.store(false);
        return;
    }

    base_path_ = decisions[0].base_path;
    group_key_ = decisions[0].group_key;
    raw_json_  = decisions[0].raw_json;

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

void HistoryWorker::restore_entry(void* arg) {
    RestoreCtx* ctx = static_cast<RestoreCtx*>(arg);
    ctx->self->restore_worker(ctx->entry, ctx->index);
}

void HistoryWorker::restore_worker(const HistoryEntry& entry, size_t index) {
    (void)index;
    printf("[history] restoring hash=%s for %s\n",
           entry.hash.size() > 12 ? entry.hash.substr(0, 12).c_str()
                                  : entry.hash.c_str(),
           title_.name.c_str());

    WebDavCfg dav = session_->dav.as_cfg();
    int rc = restore_remote_save(session_->vault, entry.hash,
                                 base_path_, group_key_, raw_json_,
                                 title_, dav);
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
