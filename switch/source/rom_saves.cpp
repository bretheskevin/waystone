#include "rom_saves.h"
#include "rom_parse.h"
#include "rom_io.h"
#include "rom_art.h"
#include "rom_boxart.h"
#include "snapshot.h"  // fs_mkdir_p
#include "wall_clock.h"

#include <cstdio>
#include <cstdint>
#include <ctime>
#include <sys/stat.h>
#include <string>
#include <vector>

static const char* const ROMS_ROOTS[] = {ROM_NATIVE_ROMS, ROM_RETROARCH_ROMS};
static const char ROM_ICON_DIR[] = "sdmc:/waystone/icons";
static const unsigned ROM_BOXART_ICON_SIDE = 128;  // dashboard tile is 48 px; headroom for scaling
static const RomBoxartLimits RETROARCH_BOXART_LIMITS = { 2048, 2048, 2 * 1024 * 1024 };

// Writes a side*side RGBA8888 icon as a BMP cache file; a partial file is removed. false on failure.
static bool write_icon_bmp(const std::string& path, const std::vector<uint8_t>& rgba, unsigned side) {
    std::vector<uint8_t> bmp = rom_encode_bmp_rgba(rgba.empty() ? 0 : &rgba[0], (int)side, (int)side);
    if (bmp.empty()) { printf("[roms] icon encode failed for %s\n", path.c_str()); return false; }
    if (!fs_mkdir_p(ROM_ICON_DIR)) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("[roms] icon write failed (fopen %s)\n", path.c_str()); return false; }
    bool ok = fwrite(bmp.data(), 1, bmp.size(), f) == bmp.size();
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        printf("[roms] icon write failed (%s), removing partial file\n", path.c_str());
        remove(path.c_str());
        return false;
    }
    printf("[roms] icon cached %s (%ux%u)\n", path.c_str(), side, side);
    return true;
}

// Decode the NDS banner into a cached BMP icon + title. Returns the icon path ("" on failure).
static std::string nds_banner_icon(const std::string& abs, const std::string& rom_id,
                                   const std::vector<uint8_t>& header, std::string* title) {
    std::vector<uint8_t> banner, rgba;
    if (!rom_read_nds_banner(abs, header, &banner) || !rom_decode_nds_banner(banner, &rgba, title)) {
        printf("[roms] nds banner unavailable for %s\n", abs.c_str());
        return std::string();
    }
    std::string icon = std::string(ROM_ICON_DIR) + "/rom-nds-" + rom_id + ".bmp";
    struct stat ist;
    if (stat(icon.c_str(), &ist) == 0 && ist.st_size > 0) return icon;
    return write_icon_bmp(icon, rgba, ROM_NDS_ICON_SIDE) ? icon : std::string();
}

// RetroArch thumbnail decoded + downscaled here (scan worker) into a small cached BMP, so the UI
// thread never loads full-size PNGs. The cache is rebuilt when the thumbnail is newer. "" when none.
static std::string retroarch_boxart_icon(const std::string& system, const std::string& rom_id,
                                         const std::string& rom_file_name) {
    std::string rel = rom_retroarch_boxart_rel(system, rom_file_name);
    if (rel.empty()) return std::string();
    std::string art = rom_sd_abs(std::string(ROM_RETROARCH_ROOT) + "/" + rel);
    struct stat ast;
    if (stat(art.c_str(), &ast) != 0) {
        printf("[roms] boxart %s: none\n", rom_file_name.c_str());
        return std::string();
    }
    std::string icon = std::string(ROM_ICON_DIR) + "/rom-art-" + system + "-" + rom_id + ".bmp";
    struct stat ist;
    if (stat(icon.c_str(), &ist) == 0 && ist.st_size > 0 && ist.st_mtime >= ast.st_mtime) return icon;
    std::vector<uint8_t> rgba;
    if (!rom_boxart_load(art, RETROARCH_BOXART_LIMITS, ROM_BOXART_ICON_SIDE, &rgba)) return std::string();
    return write_icon_bmp(icon, rgba, ROM_BOXART_ICON_SIDE) ? icon : std::string();
}

std::vector<TitleInfo> scan_rom_titles() {
    unsigned long long t0 = wall_now_ms();
    std::vector<TitleInfo> out;
    std::vector<std::string> rels;
    for (const char* root : ROMS_ROOTS) rom_walk_rel(root, 0, ROM_SCAN_MAX_DEPTH, rels);
    rom_walk_rel(ROM_RETROARCH_SAVES, 0, 0, rels); // depth 0; per-core save subdirs out of scope
    printf("[roms] walked %zu file(s) in %llu ms\n", rels.size(), wall_ms_since(t0));
    if (rels.empty()) return out;

    std::vector<RomPairingRow> rows;
    if (!rom_pair_rows(rels, "native", &rows)) return out;

    RomIdScan scan;
    rom_id_scan_begin(scan);
    for (size_t i = 0; i < rows.size(); i++) {
        const RomPairingRow& row = rows[i];
        std::vector<uint8_t> header;
        std::string rom_id;
        if (!rom_id_scan_row(scan, row, &header, &rom_id)) continue;
        std::string abs = rom_sd_abs(row.rom_path);
        std::string rom_file_name = rom_base_name(row.rom_path);

        TitleInfo t;
        t.title_id = 0x8000000000000000ull | rom_synthetic_unique_id(row.system, rom_id);
        t.source = SourceRom;
        t.system = row.system;
        t.rom_id = rom_id;
        t.rom_path = abs;
        t.rom_file_name = rom_file_name;
        t.save_dir = rom_sd_abs(row.save_dir);
        for (size_t s = 0; s < row.save_paths.size(); s++) t.save_paths.push_back(rom_sd_abs(row.save_paths[s]));
        t.name = rom_display_name(row.system, header, rom_file_name);
        if (row.system == "nds") {
            std::string banner_title;
            t.icon_path = nds_banner_icon(abs, rom_id, header, &banner_title);
            if (!banner_title.empty()) t.name = banner_title;
        }
        if (t.icon_path.empty()) t.icon_path = retroarch_boxart_icon(row.system, rom_id, rom_file_name);
        printf("[roms] %s -> [%s] '%s' id=%s saves=%zu save_dir=%s\n", row.rom_path.c_str(),
               row.system.c_str(), t.name.c_str(), t.rom_id.c_str(), t.save_paths.size(),
               t.save_dir.c_str());
        out.push_back(t);
    }
    rom_id_scan_end(scan);
    printf("[roms] scan done: %zu ROM title(s), %d cache hit(s), %d hashed, %d failed in %llu ms\n",
           out.size(), scan.hits, scan.computed, scan.failed, wall_ms_since(t0));
    return out;
}

std::string rom_saves_mtime(const TitleInfo& title) {
    time_t newest = 0;
    for (size_t i = 0; i < title.save_paths.size(); i++) {
        struct stat st;
        if (stat(title.save_paths[i].c_str(), &st) == 0 && st.st_mtime > newest)
            newest = st.st_mtime;
    }
    if (newest == 0) return "";
    char buf[24];
    struct tm tmv;
    if (!gmtime_r(&newest, &tmv)) return "";
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}
