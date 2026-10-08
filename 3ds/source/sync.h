#ifndef WAYSTONE_3DS_SYNC_H
#define WAYSTONE_3DS_SYNC_H

#include "net.h"
#include "saves.h"
#include "sync_engine.h"
#include <cstdint>
#include <string>
#include <vector>

// 3DS ShellOps for the shared engine (`title` = const TitleInfo*; ctx unused).
// list_saves: extract (main + extdata | TWL | ROM) -> checkpoint/rom_keyed normalize.
// list_remote_only: TWiLight ROM slots that exist only on the server.
// write_save: routes main/extdata/TWL/ROM to write_save_files.
const ShellOps& ctr_shell_ops();

// Lightweight result of resolving a save's remote location from local data only (no network).
struct SaveLocation {
    std::string base_path;
    std::string group_key;
    std::vector<uint8_t> raw_tree;
};

std::vector<SaveLocation> resolve_save_locations(const WsVault* vault,
                                                 const TitleInfo& title,
                                                 bool* error = 0);

#endif
