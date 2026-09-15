/*
 * Preview stub for SyncController — replaces switch/source/ui/sync_controller.cpp.
 * No-op implementations so the preview links without spinning up sync threads
 * or touching Waystone FFI.
 */
#include "sync_controller.h"

SyncController::SyncController(WsVault*, AccountUid, std::string,
                               WebDavCfg, std::vector<TitleInfo>)
{}

SyncController::~SyncController()
{}

void SyncController::start() {}
void SyncController::join() {}

SyncPhase SyncController::phase() const
{
    return SyncPhase::Idle;
}

std::string SyncController::status() const
{
    return "(preview)";
}

int SyncController::pushed_count() const { return 0; }
int SyncController::restored_count() const { return 0; }

const std::vector<TitleInfo>& SyncController::titles() const
{
    return titles_;
}
