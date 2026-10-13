#ifndef WAYSTONE_ROM_SAVES_H
#define WAYSTONE_ROM_SAVES_H

#include <cstdint>
#include <string>
#include <vector>
#include "file_tree.h" // FileTreeEntry
#include "saves.h"     // TitleInfo

// Scan RetroArch/mGBA ROMs and pair their battery saves via ws_rom_pair.
// Pure POSIX FS walk — safe on the LoadingActivity worker thread, borealis-free.
// Maintains the identity cache at sdmc:/waystone/rom_ids (TSV: path size mtime
// system rom_id; a hit requires identical size+mtime+system).
std::vector<TitleInfo> scan_rom_titles();

// Read the paired save files, encoded as a binary WsFileTree with paths
// "<system>/rom/<file name>" (ws_rom_keyed_normalize consumes this).
// Empty vector on read failure / no saves.
std::vector<uint8_t> extract_rom_saves(const TitleInfo& title);

// Newest mtime of the title's save files as ISO-8601 UTC; "" when no saves.
std::string rom_saves_mtime(const TitleInfo& title);

// Write native save files into title.save_dir (single-component names only).
// 0 on success, -1 on any failure (each fallible call logged with [saves]).
int write_rom_saves(const TitleInfo& title, const std::vector<FileTreeEntry>& entries);

#endif
