#ifndef WAYSTONE_ROM_SAVES_H
#define WAYSTONE_ROM_SAVES_H

#include <cstdint>
#include <string>
#include <vector>
#include "saves.h"     // TitleInfo

// Scan RetroArch/mGBA ROMs and pair their battery saves via ws_rom_pair.
// Pure POSIX FS walk — safe on the LoadingActivity worker thread, borealis-free.
// Maintains the identity cache at sdmc:/waystone/rom_ids (TSV: path size mtime
// system rom_id; a hit requires identical size+mtime+system).
std::vector<TitleInfo> scan_rom_titles();

// Newest mtime of the title's save files as ISO-8601 UTC; "" when no saves.
std::string rom_saves_mtime(const TitleInfo& title);

#endif
