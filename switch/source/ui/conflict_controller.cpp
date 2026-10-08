#include "conflict_controller.h"
#include "session.h"
#include "sync.h"
#include "json.h"
#include <chrono>
#include <cstdio>

extern "C" {
#include "waystone.h"
}

namespace {
using Clock = std::chrono::steady_clock;
unsigned long long ms_since(Clock::time_point t0) {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}
}

ConflictController::ConflictController(WsVault* vault, AccountUid uid, std::string device_id,
                                       WebDavCfg dav, std::vector<TitleInfo> titles,
                                       WaystoneShellConfig config)
    : vault_(vault), uid_(uid), device_id_(std::move(device_id)),
      dav_url_(dav.base_url), dav_user_(dav.user), dav_pass_(dav.pass),
      dav_{dav_url_.c_str(), dav_user_.c_str(), dav_pass_.c_str()},
      titles_(std::move(titles)), config_(std::move(config)) {}

ConflictController::~ConflictController() { join(); zeroize_string(dav_pass_); }

void ConflictController::join() { if (thread_.joinable()) thread_.join(); }
ConflictPhase ConflictController::phase() const { return phase_.load(); }

std::string ConflictController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<ConflictView> ConflictController::views() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ConflictView> out;
    out.reserve(conflicts_.size());
    for (const ConflictItem& c : conflicts_) {
        ConflictView v;
        v.id = c.id;
        v.queued = c.queued;
        v.title_name = c.title_name;
        v.group_key = c.group_key;
        v.local_hash = c.local_hash;
        v.local_mtime = c.local_mtime;
        v.remote_hash = c.remote_hash;
        v.remote_device_id = c.remote_device_id;
        v.remote_mtime = c.remote_mtime;
        out.push_back(std::move(v));
    }
    return out;
}

ConflictItem* ConflictController::find_locked(uint32_t id) {
    for (auto& c : conflicts_)
        if (c.id == id) return &c;
    return nullptr;
}

void ConflictController::start_scan() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        printf("[conflict] start_scan ignored: worker busy\n");
        return;
    }
    join();
    cancel_.store(false);
    phase_.store(ConflictPhase::Scanning);
    {
        std::lock_guard<std::mutex> lk(mu_);
        conflicts_.clear();
        queue_.clear();
        scan_active_ = true;
        version_++;
        status_ = "Scanning for conflicts...";
    }
    thread_ = std::thread(&ConflictController::scan_worker, this);
}

void ConflictController::scan_worker() {
    const size_t n = titles_.size();
    const auto scan_t0 = Clock::now();
    printf("[conflict] scan begin: %zu title(s)\n", n);
    SyncEngineCfg ecfg = sync_cfg_from(config_, device_id_.c_str());
    ShellOps ops = nx_shell_ops(&uid_);
    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[conflict] webdav_session_begin failed\n");
        // conflicts_ is still empty here, so request_resolve() cannot have queued anything.
        {
            std::lock_guard<std::mutex> lk(mu_);
            scan_active_ = false;
            status_ = "Network init failed";
        }
        phase_.store(ConflictPhase::Error);
        running_.store(false);
        return;
    }

    char buf[256];
    size_t scanned = 0, skipped = 0;
    int resolved = 0, resolve_failed = 0;
    for (size_t i = 0; i < n; i++) {
        drain_queue(sess, &resolved, &resolve_failed);  // returns at once when cancelled
        if (cancel_.load()) { printf("[conflict] scan cancelled at %zu/%zu\n", i, n); break; }

        const TitleInfo& t = titles_[i];
        if (!t.has_remote) {
            printf("[conflict] skip %zu/%zu '%s' (tid=%016llX): no remote backup\n", i + 1, n,
                   t.name.c_str(), (unsigned long long)t.title_id);
            skipped++;
            continue;
        }
        snprintf(buf, sizeof(buf), "Scanning %zu/%zu: %s", i + 1, n, t.name.c_str());
        { std::lock_guard<std::mutex> lk(mu_); status_ = buf; }

        const auto t0 = Clock::now();
        std::vector<SaveDecision> decisions = sync_scan_title(
            vault_, &t, t.name.c_str(), ops, ecfg, 1 /* Prompt */, sess, nullptr, nullptr);
        scanned++;

        size_t title_conflicts = 0;
        for (SaveDecision& d : decisions) {
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
            ci.title_name = t.name;
            ci.title = t;
            ci.group_key = d.group_key;
            ci.local_hash = d.local_hash;
            ci.local_mtime = d.local_mtime;
            ci.remote_hash = remote_hash;
            ci.remote_device_id = remote_device_id;
            ci.remote_mtime = remote_mtime;
            ci.base_path = d.base_path;
            ci.raw_tree = std::move(d.raw_tree);
            const uint32_t id = ci.id;
            {
                std::lock_guard<std::mutex> lk(mu_);
                conflicts_.push_back(std::move(ci));
                version_++;
            }
            title_conflicts++;
            printf("[conflict] found id=%u %s\n", id, d.group_key.c_str());
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
        std::lock_guard<std::mutex> lk(mu_);
        if (queue_.empty() || cancel_.load()) {
            dropped = queue_.size();
            queue_.clear();
            scan_active_ = false;
            cancelled = cancel_.load();
            found = conflicts_.size();
            if (resolve_failed > 0)
                snprintf(buf, sizeof(buf), "Scan complete: %zu conflict(s), %d resolve error(s)",
                         found, resolve_failed);
            else
                snprintf(buf, sizeof(buf), "Scan complete: %zu conflict(s) found", found);
            status_ = buf;
            break;
        }
    }
    webdav_session_end(sess);

    if (dropped) printf("[conflict] dropped %zu queued resolve(s) on cancel\n", dropped);
    printf("[conflict] scan %s: scanned=%zu skipped=%zu found=%zu resolved=%d resolve_failed=%d in %llu ms\n",
           cancelled ? "cancelled" : "done", scanned, skipped, found, resolved, resolve_failed,
           ms_since(scan_t0));
    phase_.store(found > 0 ? ConflictPhase::Ready : ConflictPhase::Done);
    running_.store(false);
}

void ConflictController::drain_queue(WebDavSession* sess, int* resolved, int* failed) {
    for (;;) {
        if (cancel_.load()) return;
        ResolveReq req;
        size_t left;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (queue_.empty()) return;
            req = queue_.front();
            queue_.erase(queue_.begin());
            left = queue_.size();
        }
        printf("[conflict] drain: id=%u %s (%zu more queued)\n", req.id,
               req.keep_local ? "keep-local" : "keep-remote", left);
        ResolveResult r = resolve_one(sess, req.id, req.keep_local);
        if (r == RR_OK) (*resolved)++;
        else if (r == RR_FAILED) (*failed)++;
    }
}

void ConflictController::request_resolve(uint32_t id, bool keep_local) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        ConflictItem* p = find_locked(id);
        if (!p) {
            printf("[conflict] resolve id=%u ignored: unknown (already resolved)\n", id);
            return;
        }
        if (p->queued) {
            printf("[conflict] resolve id=%u ignored: already queued\n", id);
            return;
        }
        if (scan_active_) {
            queue_.push_back(ResolveReq{id, keep_local});
            p->queued = true;
            version_++;
            printf("[conflict] enqueued id=%u %s %s (queue=%zu)\n", id,
                   keep_local ? "keep-local" : "keep-remote", p->group_key.c_str(), queue_.size());
            return;
        }
    }
    start_resolve(id, keep_local);
}

void ConflictController::start_resolve(uint32_t id, bool keep_local) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        printf("[conflict] resolve id=%u ignored: worker busy\n", id);
        return;
    }
    join();
    phase_.store(ConflictPhase::Resolving);
    thread_ = std::thread(&ConflictController::resolve_worker, this, id, keep_local);
    printf("[conflict] resolve thread started id=%u %s\n", id, keep_local ? "keep-local" : "keep-remote");
}

void ConflictController::resolve_worker(uint32_t id, bool keep_local) {
    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[conflict] webdav_session_begin failed (resolve id=%u)\n", id);
        {
            std::lock_guard<std::mutex> lk(mu_);
            status_ = "Network init failed";
            phase_.store(conflicts_.empty() ? ConflictPhase::Done : ConflictPhase::Ready);
        }
        running_.store(false);
        return;
    }
    resolve_one(sess, id, keep_local);
    webdav_session_end(sess);
    {
        std::lock_guard<std::mutex> lk(mu_);
        phase_.store(conflicts_.empty() ? ConflictPhase::Done : ConflictPhase::Ready);
    }
    running_.store(false);
}

// Shared push/restore body for both the resolve thread and the scan-thread drain.
ConflictController::ResolveResult ConflictController::resolve_one(WebDavSession* sess, uint32_t id,
                                                                  bool keep_local) {
    ConflictItem item;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ConflictItem* p = find_locked(id);
        if (!p) {
            printf("[conflict] resolve id=%u skipped: unknown (already resolved)\n", id);
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
        status_ = std::string(keep_local ? "Pushing local: " : "Restoring remote: ") + item.group_key;
    }

    printf("[conflict] resolve id=%u %s %s begin\n", id, keep_local ? "keep-local" : "keep-remote",
           item.group_key.c_str());
    const auto t0 = Clock::now();
    const TitleInfo& ti = item.title;
    SyncEngineCfg ecfg = sync_cfg_from(config_, device_id_.c_str());
    ShellOps ops = nx_shell_ops(&uid_);
    int rc;
    if (keep_local) {
        rc = sync_push_group(vault_, &ti, ti.name.c_str(), ops, ecfg, item.group_key, sess, nullptr);
    } else {
        rc = sync_pull_hash(vault_, &ti, ops, ecfg, item.base_path, item.group_key,
                            item.remote_hash, item.remote_mtime, item.raw_tree, sess, nullptr);
    }
    const bool ok = (rc == 0);
    printf("[conflict] resolve id=%u %s (rc=%d, %llu ms)\n", id, ok ? "done" : "failed", rc, ms_since(t0));

    std::lock_guard<std::mutex> lk(mu_);
    ConflictItem* q = find_locked(id);
    if (ok) {
        if (q) conflicts_.erase(conflicts_.begin() + (q - conflicts_.data()));
        status_ = std::string(keep_local ? "Resolved (kept local): " : "Resolved (kept remote): ") + item.group_key;
    } else {
        if (q) {
            q->queued = false;
            if (!keep_local) q->raw_tree.swap(item.raw_tree);
        }
        status_ = std::string(keep_local ? "Error pushing: " : "Error restoring: ") + item.group_key;
    }
    version_++;
    return ok ? RR_OK : RR_FAILED;
}
