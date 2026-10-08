/*
 * Preview stub for switch/source/ui/conflict_controller.{h,cpp}.
 * Provides 3 canned conflicts in ConflictPhase::Ready state.
 * resolve_keep_local/remote remove the item from the list (no network).
 * Linked INSTEAD of the real conflict_controller.cpp in the preview build.
 */
#include "conflict_controller.h"

ConflictController::ConflictController(WsVault* /*vault*/, AccountUid /*uid*/,
                                       std::string /*device_id*/, WebDavCfg /*dav*/,
                                       std::vector<TitleInfo> /*titles*/,
                                       WaystoneShellConfig config)
    : vault_(nullptr), uid_({}), dav_({nullptr, nullptr, nullptr}), config_(config) {}

ConflictController::~ConflictController() {}

void ConflictController::start_scan() {
    auto add = [this](const char* name, const char* key, const char* lh, const char* lm,
                      const char* rh, const char* rd, const char* rm) {
        ConflictItem c;
        c.id = next_id_++;
        c.title_name = name;
        c.group_key = key;
        c.local_hash = lh;
        c.local_mtime = lm;
        c.remote_hash = rh;
        c.remote_device_id = rd;
        c.remote_mtime = rm;
        conflicts_.push_back(std::move(c));
    };
    {
        std::lock_guard<std::mutex> lk(mu_);
        conflicts_.clear();
        add("Animal Crossing: New Horizons", "switch/animal-crossing.key/slot0", "aabbccdd1111",
            "2026-09-14T18:30:00Z", "eeff00112222", "remote-dev-001", "2026-09-15T09:00:00Z");
        add("Zelda: TotK", "switch/zelda-totk.key/slot0", "11223344aaaa",
            "2026-09-13T10:00:00Z", "55667788bbbb", "remote-dev-002", "2026-09-14T22:15:00Z");
        add("Celeste", "switch/celeste.key/slot0", "deadbeef0000",
            "2026-09-12T08:00:00Z", "cafebabe1111", "remote-dev-003", "2026-09-13T16:45:00Z");
        status_ = "Scan complete: 3 conflict(s) found";
        version_++;
    }
    phase_.store(ConflictPhase::Ready);
    running_.store(false);
}

void ConflictController::request_resolve(uint32_t id, bool keep_local) {
    std::lock_guard<std::mutex> lk(mu_);
    ConflictItem* p = find_locked(id);
    if (!p) return;
    status_ = std::string(keep_local ? "Resolved (kept local): " : "Resolved (kept remote): ") + p->group_key;
    conflicts_.erase(conflicts_.begin() + (p - conflicts_.data()));
    version_++;
    phase_.store(conflicts_.empty() ? ConflictPhase::Done : ConflictPhase::Ready);
}

void ConflictController::join() {}

ConflictPhase ConflictController::phase() const { return phase_.load(); }

std::string ConflictController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<ConflictView> ConflictController::views() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ConflictView> out;
    for (const auto& c : conflicts_) {
        ConflictView v;
        v.id = c.id; v.queued = c.queued; v.title_name = c.title_name; v.group_key = c.group_key;
        v.local_hash = c.local_hash; v.local_mtime = c.local_mtime; v.remote_hash = c.remote_hash;
        v.remote_device_id = c.remote_device_id; v.remote_mtime = c.remote_mtime;
        out.push_back(std::move(v));
    }
    return out;
}

ConflictItem* ConflictController::find_locked(uint32_t id) {
    for (auto& c : conflicts_)
        if (c.id == id) return &c;
    return nullptr;
}

// Stub no-ops (never called through the activity layer)
void ConflictController::scan_worker() {}
void ConflictController::drain_queue(WebDavSession*, int*, int*) {}
void ConflictController::start_resolve(uint32_t, bool) {}
void ConflictController::resolve_worker(uint32_t, bool) {}
ConflictController::ResolveResult ConflictController::resolve_one(WebDavSession*, uint32_t, bool) {
    return RR_UNKNOWN;
}
