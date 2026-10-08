#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <3ds.h>
#include "conflict_core.h"
#include "net.h"
#include "saves.h"
#include "wsconfig.h"

class ConflictWorker {
public:
    ConflictWorker(WsVault* vault, const std::string& device_id,
                   const WebDavCfg& dav, std::vector<TitleInfo> titles,
                   const WaystoneShellConfig& config);
    ~ConflictWorker();

    void start_scan();
    // Queued on the scan thread while a scan runs, otherwise run on a resolve thread.
    // Unknown (already resolved) or already-queued ids are ignored.
    void resolve_keep_local(u32 id)  { request_resolve(id, true); }
    void resolve_keep_remote(u32 id) { request_resolve(id, false); }
    void join();
    // Stops the scan before the next title (and stops draining queued resolves) so a reaped
    // worker finishes quickly instead of blocking the render thread.
    void request_cancel() { core_.request_cancel(); }
    bool is_running() const { return core_.is_running(); }

    ConflictPhase phase() const { return core_.phase(); }
    std::string status() const { return core_.status(); }
    u32 version() const { return core_.version(); }
    std::vector<ConflictView> views() const { return core_.views(); }

private:
    struct ResolveCtx {
        ConflictWorker* self;
        bool keep_local;
        u32 id;
    };

    // Declaration order matters: core_ holds &mu_.
    LightLock mu_;
    ConflictCore<TitleInfo> core_;
    Thread thread_;
    ResolveCtx* pending_resolve_;   // owned; freed on the next resolve or in the dtor

    static void scan_entry(void* arg);
    static void resolve_entry(void* arg);
    void request_resolve(u32 id, bool keep_local);

    ConflictWorker(const ConflictWorker&);
    ConflictWorker& operator=(const ConflictWorker&);
};
