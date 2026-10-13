#ifndef WAYSTONE_ROM_PARSE_H
#define WAYSTONE_ROM_PARSE_H

// Pure (console-free, host-tested) helpers for the ROM-save scan, shared by the console shells.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static const size_t ROM_HEADER_BYTES = 0x200;   // == core rom_id::ROM_HEADER_LEN

struct RomPairingRow {
    std::string system;
    std::string rom_path;   // SD-root-relative
    std::string save_dir;   // SD-root-relative
    std::vector<std::string> save_paths;
};

// Parse ws_rom_pair output ("sys\trom\tsave_dir[\tsave]*\n" lines). false on a line with < 3 fields.
bool parse_rom_pair_tsv(const char* data, size_t len, std::vector<RomPairingRow>* out);

struct RomIdCacheEntry {
    std::string path;            // SD-root-relative ROM path
    unsigned long long size;
    long long mtime;
    std::string system;
    std::string rom_id;
};

// sdmc:/waystone/rom_ids line format: "path\tsize\tmtime\tsystem\trom_id" (no trailing newline).
bool parse_rom_id_cache_line(const std::string& line, RomIdCacheEntry* out);
std::string format_rom_id_cache_line(const RomIdCacheEntry& e);

const char* rom_system_badge(const std::string& system);   // "nds"->"DS", unknown->"ROM"
uint32_t rom_fnv1a(const std::string& s);                  // FNV-1a 32-bit (ids, UI tints)
bool rom_needs_header(const std::string& system);          // header-identity systems (nds, gba, n64)
uint32_t rom_synthetic_unique_id(const std::string& system, const std::string& rom_id);
// "<system>/<rom_id>[/<slot>]" — mirrors core build_group_key; slot 0 yields the game prefix only.
std::string rom_group_key(const std::string& system, const std::string& rom_id, const char* slot);
bool rom_save_name_safe(const std::string& name);          // single path component, not "."/".."
bool title_name_less(const std::string& a, const std::string& b); // ASCII case-insensitive
bool rom_hidden_name(const char* name);                    // dot entries (macOS "._*" AppleDouble, ".DS_Store", ".", "..")

#endif
