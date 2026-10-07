#ifndef WAYSTONE_3DS_ROM_PARSE_H
#define WAYSTONE_3DS_ROM_PARSE_H

// Pure (libctru-free, host-tested) helpers for the TWiLight ROM-save scan.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static const size_t ROM_HEADER_BYTES = 0x200;   // == core rom_id::ROM_HEADER_LEN
static const size_t NDS_BANNER_BYTES = 0x840;
static const unsigned NDS_ICON_SIDE = 32;
static const size_t NDS_ICON_BYTES   = NDS_ICON_SIDE * NDS_ICON_SIDE * 2;
static const unsigned BOXART_ICON_SIDE = 48;
static const size_t BOXART_ICON_BYTES = BOXART_ICON_SIDE * BOXART_ICON_SIDE * 2;
static const int BOXART_MAX_W = 256;                    // TWiLight Menu++'s own box-art limit
static const int BOXART_MAX_H = 192;
static const size_t BOXART_MAX_FILE_BYTES = 256 * 1024;

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

// NDS header 0x68 banner offset; 0 when absent or header too short.
uint32_t nds_banner_offset(const uint8_t* header, size_t len);
// English banner title (0x340): all lines but the last (publisher) joined by ' '; "" if unavailable.
std::string nds_banner_title(const uint8_t* banner, size_t len);
// 32x32 icon -> RGBA5551 linear row-major little-endian (NDS_ICON_BYTES); palette index 0 transparent.
bool nds_banner_icon_rgba5551(const uint8_t* banner, size_t len, std::vector<uint8_t>* out);

std::string utf16le_to_utf8(const uint8_t* p, size_t max_units);
const char* rom_system_badge(const std::string& system);   // "nds"->"DS", unknown->"ROM"
bool rom_needs_header(const std::string& system);          // header-identity systems (nds, gba)
uint32_t rom_synthetic_unique_id(const std::string& system, const std::string& rom_id);
// "<system>/<rom_id>[/<slot>]" — mirrors core build_group_key; slot 0 yields the game prefix only.
std::string rom_group_key(const std::string& system, const std::string& rom_id, const char* slot);
bool rom_save_name_safe(const std::string& name);          // single path component, not "."/".."
bool title_name_less(const std::string& a, const std::string& b); // ASCII case-insensitive
bool rom_hidden_name(const char* name);                    // dot entries (macOS "._*" AppleDouble, ".DS_Store", ".", "..")

// TWiLight Menu++ box art for a non-NDS ROM, SD-root-relative, in TWiLight's lookup order.
std::vector<std::string> boxart_paths(const std::string& rom_file_name);
bool boxart_dims_ok(int w, int h);
// RGBA8888 (w*h*4) -> BOXART_ICON_SIDE^2 RGBA5551 linear LE: aspect-fit, centred, box-filtered,
// transparent padding.
bool boxart_icon_rgba5551(const uint8_t* rgba, int w, int h, std::vector<uint8_t>* out);
// Decode box-art file bytes (PNG/BMP) via stb_image into a BOXART_ICON_BYTES icon. *w/*h = source
// size when the header parsed; on failure *why is a static reason string.
bool boxart_decode_icon(const uint8_t* data, size_t len, std::vector<uint8_t>* icon,
                        int* w, int* h, const char** why);
// Side of a ROM TitleInfo.icon (NDS banner 32, box art BOXART_ICON_SIDE), 0 for unknown sizes.
unsigned rom_icon_side(size_t icon_bytes);

#endif
