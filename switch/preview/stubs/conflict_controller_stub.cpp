/*
 * Preview stub for switch/source/ui/conflict_controller.{h,cpp}.
 * Provides 3 canned conflicts in ConflictPhase::Ready state.
 * resolve_keep_local/remote remove the item from the list (no network).
 * Linked INSTEAD of the real conflict_controller.cpp in the preview build.
 */
#include "conflict_controller.h"

ConflictController::ConflictController(WsVault* /*vault*/, AccountUid /*uid*/,
                                       std::string /*device_id*/, WebDavCfg /*dav*/,
                                       std::vector<TitleInfo> /*titles*/)
    : vault_(nullptr), uid_({}), dav_({nullptr, nullptr, nullptr}) {}

ConflictController::~ConflictController() {}

void ConflictController::start_scan() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        conflicts_.clear();

        ConflictItem a;
        a.title_name       = "Animal Crossing: New Horizons";
        a.title_id         = 0x01006F8002326000ULL;
        a.group_key        = "switch/animal-crossing.key/slot0";
        a.local_hash       = "aabbccdd1111";
        a.local_mtime      = "2026-09-14T18:30:00Z";
        a.remote_hash      = "eeff00112222";
        a.remote_device_id = "remote-dev-001";
        a.remote_mtime     = "2026-09-15T09:00:00Z";
        conflicts_.push_back(std::move(a));

        ConflictItem b;
        b.title_name       = "Zelda: TotK";
        b.title_id         = 0x0100F2C0115B6000ULL;
        b.group_key        = "switch/zelda-totk.key/slot0";
        b.local_hash       = "11223344aaaa";
        b.local_mtime      = "2026-09-13T10:00:00Z";
        b.remote_hash      = "55667788bbbb";
        b.remote_device_id = "remote-dev-002";
        b.remote_mtime     = "2026-09-14T22:15:00Z";
        conflicts_.push_back(std::move(b));

        ConflictItem c;
        c.title_name       = "Celeste";
        c.title_id         = 0x01002B30028F6000ULL;
        c.group_key        = "switch/celeste.key/slot0";
        c.local_hash       = "deadbeef0000";
        c.local_mtime      = "2026-09-12T08:00:00Z";
        c.remote_hash      = "cafebabe1111";
        c.remote_device_id = "remote-dev-003";
        c.remote_mtime     = "2026-09-13T16:45:00Z";
        conflicts_.push_back(std::move(c));

        status_ = "Scan complete: 3 conflict(s) found";
    }
    phase_.store(ConflictPhase::Ready);
    running_.store(false);
}

void ConflictController::resolve_keep_local(size_t index) {
    std::lock_guard<std::mutex> lk(mu_);
    if (index < conflicts_.size()) {
        std::string key = conflicts_[index].group_key;
        conflicts_.erase(conflicts_.begin() + static_cast<long>(index));
        status_ = "Resolved (kept local): " + key;
    }
    phase_.store(conflicts_.empty() ? ConflictPhase::Done : ConflictPhase::Ready);
}

void ConflictController::resolve_keep_remote(size_t index) {
    std::lock_guard<std::mutex> lk(mu_);
    if (index < conflicts_.size()) {
        std::string key = conflicts_[index].group_key;
        conflicts_.erase(conflicts_.begin() + static_cast<long>(index));
        status_ = "Resolved (kept remote): " + key;
    }
    phase_.store(conflicts_.empty() ? ConflictPhase::Done : ConflictPhase::Ready);
}

void ConflictController::join() {}

ConflictPhase ConflictController::phase() const { return phase_.load(); }

std::string ConflictController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<ConflictItem> ConflictController::conflicts() const {
    std::lock_guard<std::mutex> lk(mu_);
    return conflicts_;
}

// Private methods — stub no-ops (never called through the activity layer)
void ConflictController::scan_worker() {}
void ConflictController::resolve_impl(bool, size_t) {}
void ConflictController::resolve_worker(bool, ConflictItem, size_t) {}
