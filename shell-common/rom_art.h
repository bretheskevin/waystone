#ifndef WAYSTONE_ROM_ART_H
#define WAYSTONE_ROM_ART_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static const size_t ROM_NDS_BANNER_BYTES = 0x840;
static const unsigned ROM_NDS_ICON_SIDE = 32;
static const size_t ROM_NDS_ICON_RGBA_BYTES = ROM_NDS_ICON_SIDE * ROM_NDS_ICON_SIDE * 4;

// Reads the NDS banner (header 0x68 offset) of the ROM at abs. false when absent/unreadable.
bool rom_read_nds_banner(const std::string& abs, const std::vector<uint8_t>& header,
                         std::vector<uint8_t>* banner);
// Core decode (ws_nds_banner_icon_rgba / ws_nds_banner_title): 32x32 RGBA8888 + English title.
bool rom_decode_nds_banner(const std::vector<uint8_t>& banner, std::vector<uint8_t>* rgba,
                           std::string* title);
// 32-bpp BI_RGB bottom-up BMP (BGRA) loadable by stb_image/nanovg.
std::vector<uint8_t> rom_encode_bmp_rgba(const uint8_t* rgba, int w, int h);
// Core's placeholder for an external flavor's save root in ws_rom_pair_flavored input/output.
static const char* const ROM_EXTERNAL_SAVES = "@saves";
// "@saves/x" -> "<external_root>/x", "@saves" -> external_root; other paths unchanged.
std::string rom_external_rel(const std::string& path, const std::string& external_root);
// Newline list from ws_twilight_boxart_paths (SD-root-relative), [] on failure.
std::vector<std::string> rom_twilight_boxart_paths(const std::string& rom_file_name);
// ws_retroarch_boxart_path ("thumbnails/<playlist>/Named_Boxarts/<label>.png"), "" when none.
std::string rom_retroarch_boxart_rel(const std::string& system, const std::string& rom_file_name);
// Core display name (ws_rom_display_name); rom_file_name when the FFI call fails.
std::string rom_display_name(const std::string& system, const std::vector<uint8_t>& header,
                             const std::string& rom_file_name);
#endif
