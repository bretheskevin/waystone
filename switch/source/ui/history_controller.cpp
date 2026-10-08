#include "history_controller.h"
#include "sync.h"
#include "json.h"
#include "snapshot_browse.h" // human_timestamp
#include <cstdio>

extern "C" {
#include "waystone.h"
}

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
    if (thread_.joinable()) thread_.join();
    phase_.store(BrowsePhase::Scanning);
    { std::lock_guard<std::mutex> lk(mu_);
      status_ = "Scanning history...";
      entries_.clear(); }
    thread_ = std::thread(&HistoryController::scan_worker, this);
}

void HistoryController::scan_worker() {
    printf("[history] scanning for %s\n", title_.name.c_str());

    WebDavCfg dav = session_->dav.as_cfg();
    WebDavSession* sess = webdav_session_begin(dav);
    if (!sess) {
        printf("[history] webdav_session_begin failed\n");
        { std::lock_guard<std::mutex> lk(mu_); status_ = "Network init failed."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }
    bool had_error = false;
    SyncEngineCfg ecfg = sync_cfg_from(session_->config, session_->device_id.c_str());
    std::vector<SaveDecision> decisions = sync_scan_title(
        session_->vault, &title_, title_.name.c_str(), nx_shell_ops(&session_->uid), ecfg,
        0 /* NewestWins */, sess, &had_error, nullptr);
    webdav_session_end(sess);
    if (had_error) {
        { std::lock_guard<std::mutex> lk(mu_); status_ = "Normalize failed."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }
    if (decisions.empty()) {
        printf("[history] no local save for %s\n", title_.name.c_str());
        { std::lock_guard<std::mutex> lk(mu_); status_ = "No local save found."; }
        phase_.store(BrowsePhase::Done);
        running_.store(false);
        return;
    }

    // Use the primary save (first decision) for the remote base path
    const SaveDecision& d = decisions[0];
    if (d.base_path.empty()) {
        { std::lock_guard<std::mutex> lk(mu_); status_ = "Could not determine remote path."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }

    // Stash for restore (written only here, read only by restore_worker — no race)
    base_path_ = d.base_path;
    group_key_ = d.group_key;
    raw_tree_  = d.raw_tree;

    // List remote history
    printf("[history] listing %s/history/\n", d.base_path.c_str());
    { std::lock_guard<std::mutex> lk(mu_); status_ = "Fetching history..."; }
    std::vector<HistoryEntry> results = list_history(session_->vault, d.base_path, dav);

    {
        std::lock_guard<std::mutex> lk(mu_);
        entries_ = results;
        char buf[128];
        snprintf(buf, sizeof(buf), "%zu history version(s) found", results.size());
        status_ = buf;
    }
    phase_.store(results.empty() ? BrowsePhase::Done : BrowsePhase::Ready);
    running_.store(false);
}

void HistoryController::start_restore(size_t index) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    HistoryEntry entry;
    { std::lock_guard<std::mutex> lk(mu_);
      if (index >= entries_.size()) { running_.store(false); return; }
      entry = entries_[index]; }
    if (thread_.joinable()) thread_.join();
    phase_.store(BrowsePhase::Restoring);
    { std::lock_guard<std::mutex> lk(mu_);
      status_ = "Restoring " + human_timestamp(entry.timestamp) + "..."; }
    thread_ = std::thread(&HistoryController::restore_worker, this,
                          std::move(entry), index);
}

void HistoryController::restore_worker(HistoryEntry entry, size_t index) {
    (void)index;
    WebDavCfg dav = session_->dav.as_cfg();

    // Hash is resolved lazily (list_history only did a single PROPFIND), so fetch
    // this one entry's head now to learn which blob to restore.
    std::string hash = fetch_history_hash(session_->vault, entry, dav);
    if (hash.empty()) {
        printf("[history] could not resolve hash for %s\n", title_.name.c_str());
        { std::lock_guard<std::mutex> lk(mu_); status_ = "Could not read version."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }

    printf("[history] restoring hash=%.12s for %s\n", hash.c_str(), title_.name.c_str());

    WebDavSession* sess = webdav_session_begin(dav);
    if (!sess) {
        printf("[history] webdav_session_begin failed\n");
        { std::lock_guard<std::mutex> lk(mu_); status_ = "Network init failed."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }
    SyncEngineCfg ecfg = sync_cfg_from(session_->config, session_->device_id.c_str());
    int rc = sync_restore_hash(session_->vault, &title_, nx_shell_ops(&session_->uid), ecfg,
                               base_path_, group_key_, hash, raw_tree_, sess, nullptr);
    webdav_session_end(sess);
    if (rc != 0) {
        printf("[history] restore FAILED (rc=%d)\n", rc);
        { std::lock_guard<std::mutex> lk(mu_); status_ = "Restore failed."; }
        phase_.store(BrowsePhase::Error);
        running_.store(false);
        return;
    }

    printf("[history] restore complete\n");
    { std::lock_guard<std::mutex> lk(mu_); status_ = session_->config.safety_backup ? "Restored! Safety backup saved." : "Restored!"; }
    phase_.store(BrowsePhase::Done);
    running_.store(false);
}
