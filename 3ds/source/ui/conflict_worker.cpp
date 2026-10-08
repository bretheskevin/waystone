#include "conflict_worker.h"
#include "session.h"         // zeroize_string
#include "worker_thread.h"   // start_worker_thread
#include "sync.h"            // ctr_shell_ops, sync_scan_title, sync_push_group, sync_pull_hash
#include "json.h"
#include <cstdio>
#include <cstring>
#include <utility>

extern "C" {
struct Vault;
#include "waystone.h"
}

static unsigned long long ms_since(u64 t0) {
    return (unsigned long long)(osGetTime() - t0);
}

ConflictWorker::ConflictWorker(WsVault* vault, const std::string& device_id,
                               const WebDavCfg& dav, std::vector<TitleInfo> titles,
                               const WaystoneShellConfig& config)
    : vault_(vault),
      device_id_(device_id),
      dav_url_(dav.base_url),
      dav_user_(dav.user),
      dav_pass_(dav.pass),
      config_(config),
      titles_(titles),
      phase_((int)ConflictPhase::Idle),
      running_(false),
      cancel_(false),
      version_(0),
      scan_active_(false),
      next_id_(1),
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

std::vector<ConflictView> ConflictWorker::views() {
    LightLock_Lock(&mu_);
    std::vector<ConflictView> out;
    out.reserve(conflicts_.size());
    for (size_t i = 0; i < conflicts_.size(); i++) {
        const ConflictItem& c = conflicts_[i];
        ConflictView v;
        v.id = c.id;
        v.queued = c.queued;
        v.title_name = c.title_name;
        v.group_key = c.group_key;
        v.local_hash = c.local_hash;
        v.remote_hash = c.remote_hash;
        v.remote_device_id = c.remote_device_id;
        v.remote_mtime = c.remote_mtime;
        out.push_back(v);
    }
    LightLock_Unlock(&mu_);
    return out;
}

ConflictItem* ConflictWorker::find_locked(u32 id) {
    for (size_t i = 0; i < conflicts_.size(); i++)
        if (conflicts_[i].id == id) return &conflicts_[i];
    return 0;
}

void ConflictWorker::scan_entry(void* arg) {
    static_cast<ConflictWorker*>(arg)->scan_worker();
}

void ConflictWorker::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        printf("[conflict] start_scan ignored: worker busy\n");
        return;
    }
    join();
    phase_.store((int)ConflictPhase::Scanning);
    {
        LightLock_Lock(&mu_);
        conflicts_.clear();
        queue_.clear();
        scan_active_ = true;
        version_++;
        snprintf(status_buf_, sizeof(status_buf_), "Scanning for conflicts...");
        LightLock_Unlock(&mu_);
    }
    thread_ = start_worker_thread(scan_entry, this);
    if (!thread_) {
        printf("[conflict] threadCreate failed\n");
        phase_.store((int)ConflictPhase::Error);
        LightLock_Lock(&mu_);
        scan_active_ = false;
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
    }
}

void ConflictWorker::scan_worker() {
    const size_t n = titles_.size();
    const u64 scan_t0 = osGetTime();
    printf("[conflict] scan begin: %zu title(s)\n", n);
    SyncEngineCfg ecfg = sync_cfg_from(config_, device_id_.c_str());
    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[conflict] webdav_session_begin failed\n");
        // conflicts_ is still empty here, so request_resolve() cannot have queued anything.
        LightLock_Lock(&mu_);
        scan_active_ = false;
        snprintf(status_buf_, sizeof(status_buf_), "Network init failed");
        LightLock_Unlock(&mu_);
        phase_.store((int)ConflictPhase::Error);
        running_.store(false);
        return;
    }

    size_t scanned = 0, skipped = 0;
    int resolved = 0, resolve_failed = 0;
    for (size_t i = 0; i < n; i++) {
        drain_queue(sess, &resolved, &resolve_failed); // returns at once when cancelled
        if (cancel_.load()) { printf("[conflict] scan cancelled at %zu/%zu\n", i, n); break; }

        const TitleInfo& t = titles_[i];
        if (!t.has_remote) {
            printf("[conflict] skip %zu/%zu '%s' (tid=%016llX): no remote backup\n",
                   i + 1, n, t.name.c_str(), (unsigned long long)t.title_id);
            skipped++;
            continue;
        }
        {
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Scanning %zu/%zu: %s", i + 1, n, t.name.c_str());
            LightLock_Unlock(&mu_);
        }

        const u64 t0 = osGetTime();
        std::vector<SaveDecision> decisions =
            sync_scan_title(vault_, &t, t.name.c_str(), ctr_shell_ops(), ecfg, 1 /* Prompt */, sess, 0, 0);
        scanned++;

        size_t title_conflicts = 0;
        for (size_t si = 0; si < decisions.size(); si++) {
            SaveDecision& d = decisions[si];
            if (d.decision_type != "conflict_needs_input") continue;

            std::string remote_hash, remote_device_id, remote_mtime;
            char* folded = ws_fold_heads(d.heads_array.c_str());
            if (folded) {
                remote_hash = json_get_string(folded, "hash");
                remote_device_id = json_get_string(folded, "device_id");
                remote_mtime = json_get_string(folded, "mtime");
                ws_string_free(folded);
            } else {
                printf("[conflict] ws_fold_heads failed for %s\n", d.group_key.c_str());
            }

            ConflictItem ci;
            ci.id = next_id_++;
            ci.queued = false;
            ci.title_name = t.name;
            ci.title = t;
            ci.title.icon.clear();
            ci.group_key = d.group_key;
            ci.local_hash = d.local_hash;
            ci.local_mtime = d.local_mtime;
            ci.remote_hash = remote_hash;
            ci.remote_device_id = remote_device_id;
            ci.remote_mtime = remote_mtime;
            ci.base_path = d.base_path;
            ci.heads_array = std::move(d.heads_array);
            ci.raw_tree = std::move(d.raw_tree);
            const u32 id = ci.id;

            LightLock_Lock(&mu_);
            conflicts_.push_back(std::move(ci));
            version_++;
            LightLock_Unlock(&mu_);
            title_conflicts++;
            printf("[conflict] found id=%lu %s\n", (unsigned long)id, d.group_key.c_str());
        }
        printf("[conflict] title %zu/%zu '%s' scanned in %llu ms (saves=%zu, conflicts=%zu)\n",
               i + 1, n, t.name.c_str(), ms_since(t0), decisions.size(), title_conflicts);
    }

    // Close the queue under the lock only once it is empty, so a concurrent
    // request_resolve() either lands in a queue we still drain or falls through
    // to start_resolve().
    size_t found = 0, dropped = 0;
    bool cancelled = false;
    for (;;) {
        drain_queue(sess, &resolved, &resolve_failed);
        LightLock_Lock(&mu_);
        if (queue_.empty() || cancel_.load()) {
            dropped = queue_.size();
            queue_.clear();
            scan_active_ = false;
            cancelled = cancel_.load();
            found = conflicts_.size();
            if (resolve_failed > 0)
                snprintf(status_buf_, sizeof(status_buf_),
                         "Scan complete: %zu conflict(s), %d resolve error(s)",
                         found, resolve_failed);
            else
                snprintf(status_buf_, sizeof(status_buf_),
                         "Scan complete: %zu conflict(s) found", found);
            LightLock_Unlock(&mu_);
            break;
        }
        LightLock_Unlock(&mu_);
    }
    webdav_session_end(sess);

    if (dropped) printf("[conflict] dropped %zu queued resolve(s) on cancel\n", dropped);
    printf("[conflict] scan %s: scanned=%zu skipped=%zu found=%zu resolved=%d resolve_failed=%d in %llu ms\n",
           cancelled ? "cancelled" : "done", scanned, skipped, found,
           resolved, resolve_failed, ms_since(scan_t0));
    phase_.store(found > 0 ? (int)ConflictPhase::Ready : (int)ConflictPhase::Done);
    running_.store(false);
}

void ConflictWorker::drain_queue(WebDavSession* sess, int* resolved, int* failed) {
    for (;;) {
        if (cancel_.load()) return;
        ResolveReq req;
        size_t left;
        LightLock_Lock(&mu_);
        if (queue_.empty()) { LightLock_Unlock(&mu_); return; }
        req = queue_.front();
        queue_.erase(queue_.begin());
        left = queue_.size();
        LightLock_Unlock(&mu_);

        printf("[conflict] drain: id=%lu %s (%zu more queued)\n",
               (unsigned long)req.id, req.keep_local ? "keep-local" : "keep-remote", left);
        ResolveResult r = resolve_one(sess, req.id, req.keep_local);
        if (r == RR_OK) (*resolved)++;
        else if (r == RR_FAILED) (*failed)++;
    }
}

void ConflictWorker::request_resolve(u32 id, bool keep_local) {
    LightLock_Lock(&mu_);
    ConflictItem* p = find_locked(id);
    if (!p) {
        LightLock_Unlock(&mu_);
        printf("[conflict] resolve id=%lu ignored: unknown (already resolved)\n", (unsigned long)id);
        return;
    }
    if (p->queued) {
        LightLock_Unlock(&mu_);
        printf("[conflict] resolve id=%lu ignored: already queued\n", (unsigned long)id);
        return;
    }
    if (scan_active_) {
        ResolveReq r;
        r.id = id;
        r.keep_local = keep_local;
        queue_.push_back(r);
        p->queued = true;
        version_++;
        size_t depth = queue_.size();
        std::string gk = p->group_key;
        LightLock_Unlock(&mu_);
        printf("[conflict] enqueued id=%lu %s %s (queue=%zu)\n", (unsigned long)id,
               keep_local ? "keep-local" : "keep-remote", gk.c_str(), depth);
        return;
    }
    LightLock_Unlock(&mu_);
    start_resolve(id, keep_local);
}

void ConflictWorker::start_resolve(u32 id, bool keep_local) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        printf("[conflict] resolve id=%lu ignored: worker busy\n", (unsigned long)id);
        return;
    }
    join();
    phase_.store((int)ConflictPhase::Resolving);

    delete pending_resolve_;
    pending_resolve_ = new ResolveCtx();
    pending_resolve_->self = this;
    pending_resolve_->keep_local = keep_local;
    pending_resolve_->id = id;
    thread_ = start_worker_thread(resolve_entry, pending_resolve_);
    if (!thread_) {
        printf("[conflict] resolve threadCreate failed id=%lu\n", (unsigned long)id);
        phase_.store((int)ConflictPhase::Error);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
        return;
    }
    printf("[conflict] resolve thread started id=%lu %s\n", (unsigned long)id,
           keep_local ? "keep-local" : "keep-remote");
}

void ConflictWorker::resolve_entry(void* arg) {
    ResolveCtx* ctx = static_cast<ResolveCtx*>(arg);
    ctx->self->resolve_worker(ctx->id, ctx->keep_local);
    // ctx is owned by ConflictWorker::pending_resolve_ and freed in ~ConflictWorker or next resolve.
}

void ConflictWorker::resolve_worker(u32 id, bool keep_local) {
    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[conflict] webdav_session_begin failed (resolve id=%lu)\n", (unsigned long)id);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Network init failed");
        phase_.store(conflicts_.empty() ? (int)ConflictPhase::Done : (int)ConflictPhase::Ready);
        LightLock_Unlock(&mu_);
        running_.store(false);
        return;
    }
    resolve_one(sess, id, keep_local);
    webdav_session_end(sess);

    LightLock_Lock(&mu_);
    phase_.store(conflicts_.empty() ? (int)ConflictPhase::Done : (int)ConflictPhase::Ready);
    LightLock_Unlock(&mu_);
    running_.store(false);
}

// Shared push/restore body for both the resolve thread and the scan-thread drain.
ConflictWorker::ResolveResult ConflictWorker::resolve_one(WebDavSession* sess, u32 id,
                                                          bool keep_local) {
    ConflictItem item;
    LightLock_Lock(&mu_);
    ConflictItem* p = find_locked(id);
    if (!p) {
        LightLock_Unlock(&mu_);
        printf("[conflict] resolve id=%lu skipped: unknown (already resolved)\n", (unsigned long)id);
        return RR_UNKNOWN;
    }
    item.title_name = p->title_name;
    item.title = p->title;
    item.group_key = p->group_key;
    item.remote_hash = p->remote_hash;
    item.remote_mtime = p->remote_mtime;
    item.base_path = p->base_path;
    // Restore needs the tree (push re-extracts): borrow it instead of copying the whole save;
    // it is handed back below if the item stays in the list.
    if (!keep_local) item.raw_tree.swap(p->raw_tree);
    snprintf(status_buf_, sizeof(status_buf_), "%s: %s",
             keep_local ? "Pushing local" : "Restoring remote", item.group_key.c_str());
    LightLock_Unlock(&mu_);

    printf("[conflict] resolve id=%lu %s %s begin\n", (unsigned long)id,
           keep_local ? "keep-local" : "keep-remote", item.group_key.c_str());
    const u64 t0 = osGetTime();
    const TitleInfo& ti = item.title;
    SyncEngineCfg ecfg = sync_cfg_from(config_, device_id_.c_str());
    int rc;
    if (keep_local) {
        rc = sync_push_group(vault_, &ti, ti.name.c_str(), ctr_shell_ops(), ecfg, item.group_key, sess, 0);
    } else {
        rc = sync_pull_hash(vault_, &ti, ctr_shell_ops(), ecfg, item.base_path, item.group_key,
                            item.remote_hash, item.remote_mtime, item.raw_tree, sess, 0);
    }
    const bool ok = (rc == 0);
    printf("[conflict] resolve id=%lu %s (rc=%d, %llu ms)\n", (unsigned long)id,
           ok ? "done" : "failed", rc, ms_since(t0));

    LightLock_Lock(&mu_);
    ConflictItem* q = find_locked(id);
    if (ok) {
        if (q) conflicts_.erase(conflicts_.begin() + (q - conflicts_.data()));
        snprintf(status_buf_, sizeof(status_buf_), "Resolved (%s): %s",
                 keep_local ? "kept local" : "kept remote", item.group_key.c_str());
    } else {
        if (q) {
            q->queued = false;
            if (!keep_local) q->raw_tree.swap(item.raw_tree);
        }
        snprintf(status_buf_, sizeof(status_buf_), "Error %s: %s",
                 keep_local ? "pushing" : "restoring", item.group_key.c_str());
    }
    version_++;
    LightLock_Unlock(&mu_);
    return ok ? RR_OK : RR_FAILED;
}
