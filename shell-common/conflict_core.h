#ifndef WAYSTONE_CONFLICT_CORE_H
#define WAYSTONE_CONFLICT_CORE_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/time.h>
#include <utility>
#include <vector>
#include "net.h"
#include "secure_clear.h"
#include "sync_engine.h"
#include "wsconfig.h"

// Shell-provided mutex (std::mutex on Switch, LightLock on 3DS). Non-recursive.
struct ConflictLockOps {
    void (*lock)(void* ctx);
    void (*unlock)(void* ctx);
    void* ctx;
};

enum class ConflictPhase { Idle, Scanning, Ready, Resolving, Done, Error };

// Display-only copy (no raw_tree) handed to the screen/activity.
struct ConflictView {
    uint32_t id;
    bool queued;
    std::string title_name, group_key, local_hash, local_mtime;
    std::string remote_hash, remote_device_id, remote_mtime;
    ConflictView() : id(0), queued(false) {}
};

template <class TitleT>
struct ConflictItem {
    uint32_t id;      // stable, monotonically increasing
    bool queued;      // resolve waiting on the scan thread
    std::string title_name;
    TitleT title;
    std::string group_key, local_hash, local_mtime;
    std::string remote_hash, remote_device_id, remote_mtime;
    std::string base_path;
    std::vector<uint8_t> raw_tree;
    ConflictItem() : id(0), queued(false), title() {}
};

inline unsigned long long conflict_now_ms() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)(tv.tv_usec / 1000);
}

// Scan/queue/resolve state machine shared by the Switch ConflictController and the 3DS
// ConflictWorker. The shell owns the thread: begin_*() on the UI thread, then run_*() on the
// worker (or fail_start() if the thread could not be created).
template <class TitleT>
class ConflictCore {
public:
    ConflictCore(const WsVault* vault, const char* device_id, const WebDavCfg& dav,
                 std::vector<TitleT> titles, const WaystoneShellConfig& config,
                 const ShellOps* ops, ConflictLockOps lock, void (*prepare_item)(TitleT&) = 0)
        : vault_(vault), device_id_(device_id ? device_id : ""),
          dav_url_(dav.base_url ? dav.base_url : ""), dav_user_(dav.user ? dav.user : ""),
          dav_pass_(dav.pass ? dav.pass : ""), dav_(), titles_(std::move(titles)),
          config_(config), ops_(ops), lock_(lock), prepare_item_(prepare_item),
          phase_((int)ConflictPhase::Idle), running_(false), cancel_(false), version_(0),
          scan_active_(false), next_id_(1) {
        dav_.base_url = dav_url_.c_str();
        dav_.user = dav_user_.c_str();
        dav_.pass = dav_pass_.c_str();
    }

    ~ConflictCore() {
        if (!dav_pass_.empty()) {
            secure_clear(&dav_pass_[0], dav_pass_.size());
            dav_pass_.clear();
        }
    }

    // false = a worker is already running (nothing changed).
    bool begin_scan() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            printf("[conflict] start_scan ignored: worker busy\n");
            return false;
        }
        cancel_.store(false);
        phase_.store((int)ConflictPhase::Scanning);
        {
            Guard g(lock_);
            conflicts_.clear();
            queue_.clear();
            scan_active_ = true;
            version_++;
            status_ = "Scanning for conflicts...";
        }
        printf("[conflict] scan armed: %zu title(s)\n", titles_.size());
        return true;
    }

    void run_scan() {
        const size_t n = titles_.size();
        const unsigned long long scan_t0 = conflict_now_ms();
        printf("[conflict] scan begin: %zu title(s)\n", n);
        SyncEngineCfg ecfg = sync_cfg_from(config_, device_id_.c_str());
        WebDavSession* sess = webdav_session_begin(dav_);
        if (!sess) {
            printf("[conflict] webdav_session_begin failed\n");
            // conflicts_ is still empty here, so begin_resolve() cannot have queued anything.
            {
                Guard g(lock_);
                scan_active_ = false;
                status_ = "Network init failed";
            }
            phase_.store((int)ConflictPhase::Error);
            running_.store(false);
            return;
        }

        char buf[256];
        size_t scanned = 0, skipped = 0;
        int resolved = 0, resolve_failed = 0;
        for (size_t i = 0; i < n; i++) {
            drain_queue(sess, &resolved, &resolve_failed);  // returns at once when cancelled
            if (cancel_.load()) { printf("[conflict] scan cancelled at %zu/%zu\n", i, n); break; }

            const TitleT& t = titles_[i];
            if (!t.has_remote) {
                printf("[conflict] skip %zu/%zu '%s' (tid=%016llX): no remote backup\n", i + 1, n,
                       t.name.c_str(), (unsigned long long)t.title_id);
                skipped++;
                continue;
            }
            snprintf(buf, sizeof(buf), "Scanning %zu/%zu: %s", i + 1, n, t.name.c_str());
            { Guard g(lock_); status_ = buf; }

            const unsigned long long t0 = conflict_now_ms();
            std::vector<SaveDecision> decisions = sync_scan_title(
                vault_, &t, t.name.c_str(), *ops_, ecfg, 1 /* Prompt */, sess, 0, 0);
            scanned++;

            size_t title_conflicts = 0;
            for (size_t si = 0; si < decisions.size(); si++) {
                SaveDecision& d = decisions[si];
                if (d.decision_type != "conflict_needs_input") continue;
                Item ci;
                ci.id = next_id_++;
                ci.title_name = t.name;
                ci.title = t;
                if (prepare_item_) prepare_item_(ci.title);
                ci.group_key = d.group_key;
                ci.local_hash = d.local_hash;
                ci.local_mtime = d.local_mtime;
                ci.remote_hash = d.head_hash;
                ci.remote_device_id = d.head_device_id;
                ci.remote_mtime = d.head_mtime;
                ci.base_path = d.base_path;
                ci.raw_tree.swap(d.raw_tree);
                const uint32_t id = ci.id;
                {
                    Guard g(lock_);
                    conflicts_.push_back(std::move(ci));
                    version_++;
                }
                title_conflicts++;
                printf("[conflict] found id=%lu %s\n", (unsigned long)id, d.group_key.c_str());
            }
            printf("[conflict] title %zu/%zu '%s' scanned in %llu ms (saves=%zu, conflicts=%zu)\n",
                   i + 1, n, t.name.c_str(), conflict_now_ms() - t0, decisions.size(),
                   title_conflicts);
        }

        // Close the queue under the lock only once it is empty, so a concurrent begin_resolve()
        // either lands in a queue we still drain or falls through to a resolve thread.
        size_t found = 0, dropped = 0;
        bool cancelled = false;
        for (;;) {
            drain_queue(sess, &resolved, &resolve_failed);
            Guard g(lock_);
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
               conflict_now_ms() - scan_t0);
        phase_.store(found > 0 ? (int)ConflictPhase::Ready : (int)ConflictPhase::Done);
        running_.store(false);
    }

    // true = caller must start a thread running run_resolve(id, keep_local) (or fail_start).
    // false = queued on the scan thread, ignored (unknown / already queued), or busy.
    bool begin_resolve(uint32_t id, bool keep_local) {
        const char* how = keep_local ? "keep-local" : "keep-remote";
        enum { GO, UNKNOWN, ALREADY, QUEUED } outcome = GO;
        size_t depth = 0;
        std::string gk;
        {
            Guard g(lock_);
            Item* p = find_locked(id);
            if (!p) {
                outcome = UNKNOWN;
            } else if (p->queued) {
                outcome = ALREADY;
            } else if (scan_active_) {
                queue_.push_back(ResolveReq(id, keep_local));
                p->queued = true;
                version_++;
                depth = queue_.size();
                gk = p->group_key;
                outcome = QUEUED;
            }
        }
        if (outcome == UNKNOWN) {
            printf("[conflict] resolve id=%lu ignored: unknown (already resolved)\n", (unsigned long)id);
            return false;
        }
        if (outcome == ALREADY) {
            printf("[conflict] resolve id=%lu ignored: already queued\n", (unsigned long)id);
            return false;
        }
        if (outcome == QUEUED) {
            printf("[conflict] enqueued id=%lu %s %s (queue=%zu)\n", (unsigned long)id, how,
                   gk.c_str(), depth);
            return false;
        }
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            printf("[conflict] resolve id=%lu ignored: worker busy\n", (unsigned long)id);
            Guard g(lock_);
            status_ = "Busy \xe2\x80\x94 try again in a moment";
            return false;
        }
        phase_.store((int)ConflictPhase::Resolving);
        printf("[conflict] resolve armed id=%lu %s\n", (unsigned long)id, how);
        return true;
    }

    void run_resolve(uint32_t id, bool keep_local) {
        WebDavSession* sess = webdav_session_begin(dav_);
        if (!sess) {
            printf("[conflict] webdav_session_begin failed (resolve id=%lu)\n", (unsigned long)id);
            {
                Guard g(lock_);
                status_ = "Network init failed";
                phase_.store(conflicts_.empty() ? (int)ConflictPhase::Done : (int)ConflictPhase::Ready);
            }
            running_.store(false);
            return;
        }
        resolve_one(sess, id, keep_local);
        webdav_session_end(sess);
        {
            Guard g(lock_);
            phase_.store(conflicts_.empty() ? (int)ConflictPhase::Done : (int)ConflictPhase::Ready);
        }
        running_.store(false);
    }

    // The shell could not start the thread after a successful begin_*().
    void fail_start(const char* why, bool is_scan) {
        printf("[conflict] %s thread start failed: %s\n", is_scan ? "scan" : "resolve", why);
        phase_.store((int)ConflictPhase::Error);
        {
            Guard g(lock_);
            if (is_scan) scan_active_ = false;
            status_ = why;
        }
        running_.store(false);
    }

    // Preview harness only: canned list / instant resolve, no thread, no network.
    void debug_seed(const std::vector<ConflictView>& items, const char* status) {
        size_t count;
        {
            Guard g(lock_);
            conflicts_.clear();
            queue_.clear();
            for (size_t i = 0; i < items.size(); i++) {
                Item it;
                it.id = next_id_++;
                it.title_name = items[i].title_name;
                it.group_key = items[i].group_key;
                it.local_hash = items[i].local_hash;
                it.local_mtime = items[i].local_mtime;
                it.remote_hash = items[i].remote_hash;
                it.remote_device_id = items[i].remote_device_id;
                it.remote_mtime = items[i].remote_mtime;
                conflicts_.push_back(std::move(it));
            }
            status_ = status;
            version_++;
            count = conflicts_.size();
            phase_.store(count ? (int)ConflictPhase::Ready : (int)ConflictPhase::Done);
        }
        printf("[conflict] debug_seed: %zu item(s)\n", count);
    }

    bool debug_resolve(uint32_t id, bool keep_local) {
        bool ok = false;
        {
            Guard g(lock_);
            Item* p = find_locked(id);
            if (p) {
                status_ = std::string(keep_local ? "Resolved (kept local): " : "Resolved (kept remote): ") + p->group_key;
                conflicts_.erase(conflicts_.begin() + (p - conflicts_.data()));
                version_++;
                phase_.store(conflicts_.empty() ? (int)ConflictPhase::Done : (int)ConflictPhase::Ready);
                ok = true;
            }
        }
        printf("[conflict] debug_resolve id=%lu %s\n", (unsigned long)id, ok ? "done" : "ignored (unknown)");
        return ok;
    }

    ConflictPhase phase() const { return (ConflictPhase)phase_.load(); }
    std::string status() const { Guard g(lock_); return status_; }
    uint32_t version() const { return version_.load(); }
    void request_cancel() { cancel_.store(true); }
    bool is_running() const { return running_.load(); }

    std::vector<ConflictView> views() const {
        Guard g(lock_);
        std::vector<ConflictView> out;
        out.reserve(conflicts_.size());
        for (size_t i = 0; i < conflicts_.size(); i++) {
            const Item& c = conflicts_[i];
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
            out.push_back(v);
        }
        return out;
    }

private:
    typedef ConflictItem<TitleT> Item;
    struct ResolveReq {
        uint32_t id;
        bool keep_local;
        ResolveReq() : id(0), keep_local(false) {}
        ResolveReq(uint32_t i, bool k) : id(i), keep_local(k) {}
    };
    enum ResolveResult { RR_OK, RR_FAILED, RR_UNKNOWN };

    class Guard {
    public:
        explicit Guard(const ConflictLockOps& ops) : ops_(ops) { ops_.lock(ops_.ctx); }
        ~Guard() { ops_.unlock(ops_.ctx); }
    private:
        const ConflictLockOps& ops_;
        Guard(const Guard&);
        Guard& operator=(const Guard&);
    };

    const WsVault* vault_;
    std::string device_id_;
    std::string dav_url_, dav_user_, dav_pass_;
    WebDavCfg dav_;
    std::vector<TitleT> titles_;
    WaystoneShellConfig config_;
    const ShellOps* ops_;
    ConflictLockOps lock_;
    void (*prepare_item_)(TitleT&);
    std::atomic<int> phase_;
    std::atomic<bool> running_;
    std::atomic<bool> cancel_;
    std::atomic<uint32_t> version_;
    std::string status_;               // guarded by lock_
    std::vector<Item> conflicts_;      // guarded by lock_
    std::vector<ResolveReq> queue_;    // guarded by lock_
    bool scan_active_;                 // guarded by lock_; true => resolves are queued
    uint32_t next_id_;                 // scan thread (or debug_seed) only

    Item* find_locked(uint32_t id) {   // lock_ must be held
        for (size_t i = 0; i < conflicts_.size(); i++)
            if (conflicts_[i].id == id) return &conflicts_[i];
        return 0;
    }

    void drain_queue(WebDavSession* sess, int* resolved, int* failed) {
        for (;;) {
            if (cancel_.load()) return;
            ResolveReq req;
            size_t left;
            {
                Guard g(lock_);
                if (queue_.empty()) return;
                req = queue_.front();
                queue_.erase(queue_.begin());
                left = queue_.size();
            }
            printf("[conflict] drain: id=%lu %s (%zu more queued)\n", (unsigned long)req.id,
                   req.keep_local ? "keep-local" : "keep-remote", left);
            ResolveResult r = resolve_one(sess, req.id, req.keep_local);
            if (r == RR_OK) (*resolved)++;
            else if (r == RR_FAILED) (*failed)++;
        }
    }

    // Shared push/restore body for both the resolve thread and the scan-thread drain.
    ResolveResult resolve_one(WebDavSession* sess, uint32_t id, bool keep_local) {
        Item item;
        {
            Guard g(lock_);
            Item* p = find_locked(id);
            if (!p) {
                printf("[conflict] resolve id=%lu skipped: unknown (already resolved)\n", (unsigned long)id);
                return RR_UNKNOWN;
            }
            item.title_name = p->title_name;
            item.title = p->title;
            item.group_key = p->group_key;
            item.remote_hash = p->remote_hash;
            item.remote_mtime = p->remote_mtime;
            item.base_path = p->base_path;
            // Restore needs the tree (push re-extracts): borrow it instead of copying the whole
            // save; it is handed back below if the item stays in the list.
            if (!keep_local) item.raw_tree.swap(p->raw_tree);
            status_ = std::string(keep_local ? "Pushing local: " : "Restoring remote: ") + item.group_key;
        }

        printf("[conflict] resolve id=%lu %s %s begin\n", (unsigned long)id,
               keep_local ? "keep-local" : "keep-remote", item.group_key.c_str());
        const unsigned long long t0 = conflict_now_ms();
        const TitleT& ti = item.title;
        SyncEngineCfg ecfg = sync_cfg_from(config_, device_id_.c_str());
        int rc;
        if (keep_local) {
            rc = sync_push_group(vault_, &ti, ti.name.c_str(), *ops_, ecfg, item.group_key, sess, 0);
        } else {
            rc = sync_pull_hash(vault_, &ti, *ops_, ecfg, item.base_path, item.group_key,
                                item.remote_hash, item.remote_mtime, item.raw_tree, sess, 0);
        }
        const bool ok = (rc == 0);
        printf("[conflict] resolve id=%lu %s (rc=%d, %llu ms)\n", (unsigned long)id,
               ok ? "done" : "failed", rc, conflict_now_ms() - t0);

        Guard g(lock_);
        Item* q = find_locked(id);
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

    ConflictCore(const ConflictCore&);
    ConflictCore& operator=(const ConflictCore&);
};

#endif
