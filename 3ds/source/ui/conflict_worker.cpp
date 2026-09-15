#include "conflict_worker.h"
#include "session.h"         // zeroize_string
#include "worker_thread.h"   // start_worker_thread
#include "sync.h"            // scan_title, restore_remote_save, push_title, SaveDecision
#include "json.h"
#include <cstdio>
#include <cstring>

extern "C" {
struct Vault;
#include "waystone.h"
}

ConflictWorker::ConflictWorker(WsVault* vault, const std::string& device_id,
                               const WebDavCfg& dav, std::vector<TitleInfo> titles)
    : vault_(vault),
      device_id_(device_id),
      dav_url_(dav.base_url),
      dav_user_(dav.user),
      dav_pass_(dav.pass),
      titles_(titles),
      phase_((int)ConflictPhase::Idle),
      running_(false),
      thread_(0),
      pending_resolve_(0)
{
    dav_.base_url = dav_url_.c_str();
    dav_.user     = dav_user_.c_str();
    dav_.pass     = dav_pass_.c_str();
    LightLock_Init(&mu_);
    memset(status_buf_, 0, sizeof(status_buf_));
}

ConflictWorker::~ConflictWorker() {
    join();
    delete pending_resolve_;
    zeroize_string(dav_pass_);
}

void ConflictWorker::join() {
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = 0;
    }
}

ConflictPhase ConflictWorker::phase() const {
    return (ConflictPhase)phase_.load();
}

std::string ConflictWorker::status() {
    LightLock_Lock(&mu_);
    std::string s(status_buf_);
    LightLock_Unlock(&mu_);
    return s;
}

std::vector<ConflictItem> ConflictWorker::conflicts() {
    LightLock_Lock(&mu_);
    std::vector<ConflictItem> copy = conflicts_;
    LightLock_Unlock(&mu_);
    return copy;
}

void ConflictWorker::scan_entry(void* arg) {
    static_cast<ConflictWorker*>(arg)->scan_worker();
}

void ConflictWorker::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    join();
    phase_.store((int)ConflictPhase::Scanning);
    {
        LightLock_Lock(&mu_);
        conflicts_.clear();
        snprintf(status_buf_, sizeof(status_buf_), "Scanning for conflicts...");
        LightLock_Unlock(&mu_);
    }
    thread_ = start_worker_thread(scan_entry, this);
    if (!thread_) {
        printf("[conflict] threadCreate failed\n");
        phase_.store((int)ConflictPhase::Error);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
    }
}

void ConflictWorker::scan_worker() {
    const size_t n = titles_.size();
    for (size_t i = 0; i < n; i++) {
        {
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Scanning %zu/%zu: %s", i + 1, n, titles_[i].name.c_str());
            LightLock_Unlock(&mu_);
        }

        std::vector<SaveDecision> decisions =
            scan_title(vault_, titles_[i], device_id_.c_str(), 1 /* Prompt */, dav_);

        for (size_t si = 0; si < decisions.size(); si++) {
            const SaveDecision& d = decisions[si];
            if (d.decision_type != "conflict_needs_input") continue;

            std::string remote_hash, remote_device_id, remote_mtime;
            char* folded = ws_fold_heads(d.heads_array.c_str());
            if (folded) {
                remote_hash = json_get_string(folded, "hash");
                remote_device_id = json_get_string(folded, "device_id");
                remote_mtime = json_get_string(folded, "mtime");
                ws_string_free(folded);
            }

            ConflictItem ci;
            ci.title_name = titles_[i].name;
            ci.title_id = titles_[i].title_id;
            ci.unique_id = titles_[i].unique_id;
            ci.group_key = d.group_key;
            ci.local_hash = d.local_hash;
            ci.local_mtime = d.raw_json.empty() ? "" : current_utc_time();
            ci.remote_hash = remote_hash;
            ci.remote_device_id = remote_device_id;
            ci.remote_mtime = remote_mtime;
            ci.base_path = d.base_path;
            ci.heads_array = d.heads_array;
            ci.raw_json = d.raw_json;

            {
                LightLock_Lock(&mu_);
                conflicts_.push_back(ci);
                LightLock_Unlock(&mu_);
            }
        }
    }

    size_t found;
    {
        LightLock_Lock(&mu_);
        found = conflicts_.size();
        snprintf(status_buf_, sizeof(status_buf_),
                 "Scan complete: %zu conflict(s) found", found);
        LightLock_Unlock(&mu_);
    }
    phase_.store(found > 0 ? (int)ConflictPhase::Ready : (int)ConflictPhase::Done);
    running_.store(false);
}

void ConflictWorker::start_resolve(size_t index, bool keep_local) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;

    ConflictItem item;
    {
        LightLock_Lock(&mu_);
        if (index >= conflicts_.size()) {
            LightLock_Unlock(&mu_);
            running_.store(false);
            return;
        }
        item = conflicts_[index];
        LightLock_Unlock(&mu_);
    }
    join();
    phase_.store((int)ConflictPhase::Resolving);
    {
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "%s: %s",
                 keep_local ? "Pushing local" : "Restoring remote",
                 item.group_key.c_str());
        LightLock_Unlock(&mu_);
    }

    delete pending_resolve_;
    pending_resolve_ = new ResolveCtx();
    pending_resolve_->self = this;
    pending_resolve_->keep_local = keep_local;
    pending_resolve_->item = item;
    pending_resolve_->index = index;
    thread_ = start_worker_thread(resolve_entry, pending_resolve_);
    if (!thread_) {
        phase_.store((int)ConflictPhase::Error);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
    }
}

void ConflictWorker::resolve_entry(void* arg) {
    ResolveCtx* ctx = static_cast<ResolveCtx*>(arg);
    ctx->self->resolve_worker(ctx->keep_local, ctx->item, ctx->index);
    // ctx is owned by ConflictWorker::pending_resolve_ and freed in ~ConflictWorker or next resolve.
}

void ConflictWorker::resolve_worker(bool keep_local, ConflictItem item, size_t index) {
    int rc;
    if (keep_local) {
        TitleInfo ti;
        ti.title_id = item.title_id;
        ti.unique_id = item.unique_id;
        ti.name = item.title_name;
        rc = push_title(vault_, ti, device_id_.c_str(), dav_);
    } else {
        rc = restore_remote_save(vault_, item.remote_hash, item.base_path,
                                 item.group_key, item.raw_json, item.title_id, dav_);
    }

    {
        LightLock_Lock(&mu_);
        bool ok = keep_local ? (rc >= 0) : (rc == 0);
        if (ok) {
            if (index < conflicts_.size())
                conflicts_.erase(conflicts_.begin() + static_cast<long>(index));
            snprintf(status_buf_, sizeof(status_buf_), "Resolved (%s): %s",
                     keep_local ? "kept local" : "kept remote", item.group_key.c_str());
        } else {
            snprintf(status_buf_, sizeof(status_buf_), "Error %s: %s",
                     keep_local ? "pushing" : "restoring", item.group_key.c_str());
        }
        phase_.store(conflicts_.empty() ? (int)ConflictPhase::Done : (int)ConflictPhase::Ready);
        LightLock_Unlock(&mu_);
    }
    running_.store(false);
}
