#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "conflict_core.h"
#include "net.h"
#include "saves.h"
#include "wsconfig.h"

class ConflictController {
public:
    ConflictController(WsVault* vault, AccountUid uid, std::string device_id, WebDavCfg dav,
                       std::vector<TitleInfo> titles, WaystoneShellConfig config);
    ~ConflictController();
    ConflictController(const ConflictController&) = delete;
    ConflictController& operator=(const ConflictController&) = delete;

    void start_scan();
    // Queued on the scan thread while a scan runs, otherwise run on a resolve thread.
    // Unknown (already resolved) or already-queued ids are ignored.
    void resolve_keep_local(uint32_t id)  { request_resolve(id, true); }
    void resolve_keep_remote(uint32_t id) { request_resolve(id, false); }
    void request_cancel() { core_.request_cancel(); }
    bool is_running() const { return core_.is_running(); }
    void join();

    ConflictPhase phase() const { return core_.phase(); }
    std::string status() const { return core_.status(); }
    uint32_t version() const { return core_.version(); }
    std::vector<ConflictView> views() const { return core_.views(); }

private:
    // Declaration order matters: ops_ points at uid_, core_ points at ops_ and mu_.
    AccountUid uid_;
    ShellOps ops_;
    std::mutex mu_;
    ConflictCore<TitleInfo> core_;
    std::thread thread_;

    void request_resolve(uint32_t id, bool keep_local);

    static void mu_lock(void* m)   { static_cast<std::mutex*>(m)->lock(); }
    static void mu_unlock(void* m) { static_cast<std::mutex*>(m)->unlock(); }
    static ConflictLockOps make_lock_ops(std::mutex* m) {
        ConflictLockOps o;
        o.lock = mu_lock;
        o.unlock = mu_unlock;
        o.ctx = m;
        return o;
    }
};
