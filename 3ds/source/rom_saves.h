#ifndef WAYSTONE_3DS_ROM_SAVES_H
#define WAYSTONE_3DS_ROM_SAVES_H

// TWiLight Menu++ ROM saves on the SD card (citro2d-free; CONSOLE=1 compiles it).
#include "saves.h"
#include "file_tree.h"
#include <string>
#include <vector>

// Walk sdmc:/roms (depth <= 4), sdmc:/_nds/TWiLightMenu/saves and sdmc:/data/ngpds, pair ROMs with
// their saves via ws_rom_pair, compute identities (cached in sdmc:/waystone/rom_ids) and return one
// TitleInfo per ROM (source = SourceRom, has_remote left at its fail-open default).
std::vector<TitleInfo> scan_rom_titles();

// Read title.save_paths into a WsFileTree with paths "<system>/rom/<file name>".
// Empty vector = I/O failure; a count-0 tree = the ROM has no save yet.
std::vector<uint8_t> extract_rom_saves(const TitleInfo& title);

// Write native-named entries (single path component each) into title.save_dir (created if missing).
// Returns 0 on success, -1 if any entry failed.
int write_rom_saves(const TitleInfo& title, const std::vector<FileTreeEntry>& entries);

#endif
