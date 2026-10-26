#ifndef WAYSTONE_ROM_IO_H
#define WAYSTONE_ROM_IO_H

// Console ROM-scan IO shared by Switch + 3DS (stdio only; borealis/citro2d-free; [roms] logs).
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "rom_parse.h"
#include "file_tree.h"

static const char* const ROM_SD_PREFIX = "sdmc:/";
static const char* const ROM_ID_CACHE_PATH = "sdmc:/waystone/rom_ids";
// SD-root-relative scan roots shared by both consoles.
static const char* const ROM_NATIVE_ROMS = "roms";
static const char* const ROM_RETROARCH_ROOT = "retroarch";
static const char* const ROM_RETROARCH_ROMS = "retroarch/roms";
static const char* const ROM_RETROARCH_SAVES = "retroarch/saves";
static const int ROM_SCAN_MAX_DEPTH = 4;

std::string rom_sd_abs(const std::string& rel);
std::string rom_base_name(const std::string& path);  // last '/' component
// Recursive SD walk below rel_dir (SD-root-relative), skipping hidden entries; appends file rels.
void rom_walk_rel(const std::string& rel_dir, int depth, int max_depth, std::vector<std::string>& out);
bool rom_read_prefix(const std::string& abs, size_t n, std::vector<uint8_t>* out);
bool rom_read_whole(const std::string& abs, std::vector<uint8_t>* out);
std::string rom_lower_ext(const std::string& file_name);

// .cue -> absolute path of its first data FILE (rejects absolute/".." names).
bool rom_resolve_cue(const std::string& cue_abs, std::string* data_abs);

typedef std::map<std::string, RomIdCacheEntry> RomIdCache;
// One scan's identity cache (ROM_ID_CACHE_PATH): begin() loads it, row() per paired ROM, end() saves.
struct RomIdScan {
    RomIdCache cache, fresh;
    int hits, computed, failed;
    RomIdScan() : hits(0), computed(0), failed(0) {}
};
void rom_id_scan_begin(RomIdScan& scan);
// Header (when the system needs one) + cached-or-computed core identity (PSX: boot serial with
// sampled-CRC fallback). false, counted in scan.failed, when the ROM is unusable.
bool rom_id_scan_row(RomIdScan& scan, const RomPairingRow& row, std::vector<uint8_t>* header,
                     std::string* rom_id);
void rom_id_scan_end(const RomIdScan& scan);

// ws_rom_pair_flavored over SD-root-relative file rels ("native" = plain pairing). false on failure.
bool rom_pair_rows(const std::vector<std::string>& rels, const char* flavor,
                   std::vector<RomPairingRow>* rows);

// Append each readable save as FileTreeEntry(prefix + file name, bytes). strict: the first
// unreadable file fails the call; otherwise it is logged and skipped (treated as absent).
bool rom_read_save_entries(const std::vector<std::string>& save_paths, const std::string& prefix,
                           bool strict, std::vector<FileTreeEntry>* out);
// Read save_paths into a WsFileTree with paths "<system>/rom/<file name>" (ws_rom_keyed_normalize
// input). Empty vector = I/O failure; a count-0 tree = the ROM has no save yet.
std::vector<uint8_t> rom_extract_saves(const std::string& system, const std::vector<std::string>& save_paths);
// Write native-named entries (single path component each) into save_dir (created if missing).
// 0 on success, -1 if any entry failed.
int rom_write_saves(const std::string& save_dir, const std::vector<FileTreeEntry>& entries);
#endif
