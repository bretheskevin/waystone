#ifndef WAYSTONE_3DS_ROM_SAVES_H
#define WAYSTONE_3DS_ROM_SAVES_H

// TWiLight Menu++ ROM saves on the SD card (citro2d-free; CONSOLE=1 compiles it).
#include "saves.h"
#include <vector>

// Pass A (native): walk sdmc:/roms (depth <= 4), sdmc:/_nds/TWiLightMenu/saves and sdmc:/data/ngpds;
// pass B (retroarch): sdmc:/retroarch/roms (depth <= 4) + the retroarch.cfg save root (rom_retroarch_rows).
// Pair ROMs with their saves, compute identities (cached in sdmc:/waystone/rom_ids) and return one
// TitleInfo per ROM (source = SourceRom, has_remote left at its fail-open default).
std::vector<TitleInfo> scan_rom_titles();

#endif
