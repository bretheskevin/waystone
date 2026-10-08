#include "conflict_worker.h"
#include "worker_thread.h"   // start_worker_thread
#include "sync.h"            // ctr_shell_ops
#include <cstdio>
#include <utility>

static void ll_lock(void* p)   { LightLock_Lock(static_cast<LightLock*>(p)); }
static void ll_unlock(void* p) { LightLock_Unlock(static_cast<LightLock*>(p)); }

// Initializes the LightLock before the core that uses it is constructed.
static ConflictLockOps init_light_lock(LightLock* mu) {
    LightLock_Init(mu);
    ConflictLockOps o;
    o.lock = ll_lock;
    o.unlock = ll_unlock;
    o.ctx = mu;
    return o;
}

// TitleInfo is copied into each conflict item; the SMDH icon is not needed to push/restore.
static void drop_icon(TitleInfo& t) { t.icon.clear(); }

ConflictWorker::ConflictWorker(WsVault* vault, const std::string& device_id,
                               const WebDavCfg& dav, std::vector<TitleInfo> titles,
                               const WaystoneShellConfig& config)
    : core_(vault, device_id.c_str(), dav, std::move(titles), config, &ctr_shell_ops(),
            init_light_lock(&mu_), drop_icon),
      thread_(0),
      pending_resolve_(0) {}

ConflictWorker::~ConflictWorker() {
    join();
    delete pending_resolve_;
}

void ConflictWorker::join() {
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = 0;
    }
}

void ConflictWorker::scan_entry(void* arg) {
    static_cast<ConflictWorker*>(arg)->core_.run_scan();
}

void ConflictWorker::resolve_entry(void* arg) {
    ResolveCtx* ctx = static_cast<ResolveCtx*>(arg);
    ctx->self->core_.run_resolve(ctx->id, ctx->keep_local);
}

void ConflictWorker::start_scan() {
    if (!core_.begin_scan()) return;
    join();
    thread_ = start_worker_thread(scan_entry, this);
    if (!thread_) {
        printf("[conflict] threadCreate failed\n");
        core_.fail_start("Thread creation failed", true);
        return;
    }
    printf("[conflict] scan thread started\n");
}

void ConflictWorker::request_resolve(u32 id, bool keep_local) {
    if (!core_.begin_resolve(id, keep_local)) return;
    join();
    delete pending_resolve_;
    pending_resolve_ = new ResolveCtx();
    pending_resolve_->self = this;
    pending_resolve_->keep_local = keep_local;
    pending_resolve_->id = id;
    thread_ = start_worker_thread(resolve_entry, pending_resolve_);
    if (!thread_) {
        printf("[conflict] resolve threadCreate failed id=%lu\n", (unsigned long)id);
        core_.fail_start("Thread creation failed", false);
        return;
    }
    printf("[conflict] resolve thread started id=%lu %s\n", (unsigned long)id,
           keep_local ? "keep-local" : "keep-remote");
}
