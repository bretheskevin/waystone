#ifndef WAYSTONE_ROM_RETROARCH_H
#define WAYSTONE_ROM_RETROARCH_H

// RetroArch save layout (retroarch.cfg + installed cores' .info) for the console ROM scans.
#include <string>
#include <vector>
#include "rom_parse.h"

static const char* const ROM_RETROARCH_CFG = "retroarch/retroarch.cfg";
static const int ROM_RETROARCH_SAVE_DEPTH = 2;  // <save root>/<content dir>/<core>/X.srm

// `platform` ("switch" | "3ds") picks RetroArch's built-in default dirs for "default"/missing keys.
// SD-root-relative dirs from ws_retroarch_dirs; empty save_root = saves next to the ROM.
struct RomRetroarchDirs {
    std::string save_root, cores_dir, info_dir;
};
bool rom_retroarch_dirs(const std::string& cfg_text, const char* platform, RomRetroarchDirs* out);

// "mgba_libretro_libnx.nro" -> "<info_dir>/mgba_libretro.info"; "" when the name has no "_libretro".
std::string rom_core_info_rel(const std::string& core_file_name, const std::string& info_dir);
// Appends each walked "<root>/x" as "@saves/x" (ROM_EXTERNAL_SAVES).
void rom_append_external(const std::vector<std::string>& walked, const std::string& root,
                         std::vector<std::string>* out);

// Walks rom_roots (+ the cfg save root when set), pairs via ws_rom_pair_retroarch and maps the
// "@saves" rows back to SD-root-relative paths; each row carries the flavor its save uses ("retroarch",
// or "native" for a native-named fallback). save_root_rel = the resolved save root ("" = content dir).
bool rom_retroarch_rows(const char* platform, const std::vector<const char*>& rom_roots,
                        std::vector<RomPairingRow>* rows, std::string* save_root_rel);
#endif
