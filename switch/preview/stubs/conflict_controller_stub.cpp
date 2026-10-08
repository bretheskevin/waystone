/*
 * Preview stub for switch/source/ui/conflict_controller.cpp (linked INSTEAD of it).
 * No thread, no network: start_scan seeds 3 canned conflicts (Ready) through the shared
 * core's debug hooks, resolve_* removes the item.
 */
#include "conflict_controller.h"
#include <cstdio>

ConflictController::ConflictController(WsVault* vault, AccountUid uid, std::string device_id,
                                       WebDavCfg dav, std::vector<TitleInfo> titles,
                                       WaystoneShellConfig config)
    : uid_(uid), ops_(),
      core_(vault, device_id.c_str(), dav, std::move(titles), config, &ops_, make_lock_ops(&mu_)) {}

ConflictController::~ConflictController() {}

void ConflictController::join() {}

static ConflictView canned(const char* name, const char* key, const char* lh, const char* lm,
                           const char* rh, const char* rd, const char* rm) {
    ConflictView v;
    v.title_name = name;
    v.group_key = key;
    v.local_hash = lh;
    v.local_mtime = lm;
    v.remote_hash = rh;
    v.remote_device_id = rd;
    v.remote_mtime = rm;
    return v;
}

void ConflictController::start_scan() {
    std::vector<ConflictView> items;
    items.push_back(canned("Animal Crossing: New Horizons", "switch/animal-crossing.key/slot0",
                           "aabbccdd1111", "2026-09-14T18:30:00Z", "eeff00112222",
                           "remote-dev-001", "2026-09-15T09:00:00Z"));
    items.push_back(canned("Zelda: TotK", "switch/zelda-totk.key/slot0", "11223344aaaa",
                           "2026-09-13T10:00:00Z", "55667788bbbb", "remote-dev-002",
                           "2026-09-14T22:15:00Z"));
    items.push_back(canned("Celeste", "switch/celeste.key/slot0", "deadbeef0000",
                           "2026-09-12T08:00:00Z", "cafebabe1111", "remote-dev-003",
                           "2026-09-13T16:45:00Z"));
    core_.debug_seed(items, "Scan complete: 3 conflict(s) found");
}

void ConflictController::request_resolve(uint32_t id, bool keep_local) {
    core_.debug_resolve(id, keep_local);
}
