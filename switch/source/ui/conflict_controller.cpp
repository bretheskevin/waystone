#include "conflict_controller.h"
#include "sync.h"
#include <cstdio>

ConflictController::ConflictController(WsVault* vault, AccountUid uid, std::string device_id,
                                       WebDavCfg dav, std::vector<TitleInfo> titles,
                                       WaystoneShellConfig config)
    : uid_(uid), ops_(nx_shell_ops(&uid_)),
      core_(vault, device_id.c_str(), dav, std::move(titles), config, &ops_, make_lock_ops(&mu_)) {}

ConflictController::~ConflictController() { join(); }

void ConflictController::join() { if (thread_.joinable()) thread_.join(); }

void ConflictController::start_scan() {
    if (!core_.begin_scan()) return;
    join();
    thread_ = std::thread([this]() { core_.run_scan(); });
    printf("[conflict] scan thread started\n");
}

void ConflictController::request_resolve(uint32_t id, bool keep_local) {
    if (!core_.begin_resolve(id, keep_local)) return;
    join();
    thread_ = std::thread([this, id, keep_local]() { core_.run_resolve(id, keep_local); });
    printf("[conflict] resolve thread started id=%lu %s\n", (unsigned long)id,
           keep_local ? "keep-local" : "keep-remote");
}
