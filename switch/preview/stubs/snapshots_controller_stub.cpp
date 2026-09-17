/*
 * Preview stub for SnapshotsController.
 * No-op implementations; snapshot actions are never invoked in dashboard preview.
 */
#include "snapshots_controller.h"

SnapshotsController::SnapshotsController(TitleInfo title, AccountUid /*uid*/)
    : title_(std::move(title)), uid_{}
{}

SnapshotsController::~SnapshotsController() { join(); }

void SnapshotsController::start_scan() {}
void SnapshotsController::start_restore(size_t /*index*/) {}
void SnapshotsController::join() {}

BrowsePhase SnapshotsController::phase() const { return BrowsePhase::Idle; }
std::string SnapshotsController::status() const { return "(preview)"; }
std::vector<SnapshotEntry> SnapshotsController::snapshots() const { return {}; }
