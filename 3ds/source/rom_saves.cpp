#include "rom_saves.h"
#include "rom_parse.h"
#include "rom_io.h"
#include "rom_art.h"
#include "rom_retroarch.h"
#include "rom_icons.h"
#include "wall_clock.h"

#include <cstdio>
#include <set>
#include <sys/stat.h>

static const char* GLOBAL_SAVES  = "_nds/TWiLightMenu/saves";
static const char* NGP_SAVES     = "data/ngpds";
static const char RETROARCH_PLATFORM[] = "3ds";

static void read_nds_banner(const std::string& abs, const std::vector<uint8_t>& header,
                            std::string* name, std::vector<uint8_t>* icon) {
    std::vector<uint8_t> banner, rgba;
    if (!rom_read_nds_banner(abs, header, &banner)) return;
    if (!rom_decode_nds_banner(banner, &rgba, name) || !rgba8888_to_rgba5551(rgba, NDS_ICON_SIDE, icon))
        icon->clear();
    printf("[roms] banner %s: title='%s' icon=%s\n", abs.c_str(), name->c_str(),
           icon->empty() ? "none" : "ok");
}

// TWiLight lookup order: the first existing file wins (no .png retry after a bad .bmp).
static void read_boxart_icon(const std::string& rom_file_name, std::vector<uint8_t>* icon) {
    std::vector<std::string> rels = rom_twilight_boxart_paths(rom_file_name);
    for (size_t i = 0; i < rels.size(); i++) {
        std::string abs = rom_sd_abs(rels[i]);
        struct stat st;
        if (stat(abs.c_str(), &st) != 0) continue;
        std::vector<uint8_t> rgba;
        if (!rom_boxart_load(abs, TWILIGHT_BOXART_LIMITS, BOXART_ICON_SIDE, &rgba) ||
            !rgba8888_to_rgba5551(rgba, BOXART_ICON_SIDE, icon))
            icon->clear();
        return;
    }
    if (!rels.empty()) printf("[roms] boxart %s missing (.bmp too)\n", rom_sd_abs(rels.back()).c_str());
}

namespace {
struct ScanState {
    RomIdScan ids;
    std::set<std::string> seen;  // "system/rom_id" of titles already emitted (pass A wins)
};
}

static void scan_rows(char pass, const std::vector<RomPairingRow>& rows,
                      ScanState& state, std::vector<TitleInfo>& out) {
    for (size_t i = 0; i < rows.size(); i++) {
        const RomPairingRow& row = rows[i];
        std::vector<uint8_t> header;
        std::string rom_id;
        if (!rom_id_scan_row(state.ids, row, &header, &rom_id)) continue;
        if (!state.seen.insert(row.system + "/" + rom_id).second) {
            printf("[roms] pass=%c %s duplicates an earlier title (%s/%s), skipped\n", pass,
                   row.rom_path.c_str(), row.system.c_str(), rom_id.c_str());
            continue;
        }
        std::string abs = rom_sd_abs(row.rom_path);
        std::string rom_file_name = rom_base_name(row.rom_path);

        TitleInfo t;
        t.title_id = 0;
        t.unique_id = rom_synthetic_unique_id(row.system, rom_id);
        t.is_twl = false;
        t.source = SourceRom;
        t.system = row.system;
        t.rom_id = rom_id;
        t.rom_path = abs;
        t.rom_file_name = rom_file_name;
        t.flavor = row.flavor;
        t.save_dir = rom_sd_abs(row.save_dir);
        for (size_t s = 0; s < row.save_paths.size(); s++) t.save_paths.push_back(rom_sd_abs(row.save_paths[s]));
        if (row.system == "nds") read_nds_banner(abs, header, &t.name, &t.icon);
        else if (pass == 'A') read_boxart_icon(rom_file_name, &t.icon);
        if (t.name.empty()) t.name = rom_display_name(row.system, header, rom_file_name);
        printf("[roms] pass=%c flavor=%s %s -> [%s] '%s' id=%s saves=%zu save_dir=%s\n", pass, t.flavor.c_str(),
               row.rom_path.c_str(), row.system.c_str(), t.name.c_str(), t.rom_id.c_str(),
               t.save_paths.size(), t.save_dir.c_str());
        out.push_back(t);
    }
}

std::vector<TitleInfo> scan_rom_titles() {
    unsigned long long t0 = wall_now_ms();
    std::vector<TitleInfo> out;
    std::vector<std::string> rels_a;
    rom_walk_rel(ROM_NATIVE_ROMS, 0, ROM_SCAN_MAX_DEPTH, rels_a);
    rom_walk_rel(GLOBAL_SAVES, 0, 0, rels_a);
    rom_walk_rel(NGP_SAVES, 0, 0, rels_a);
    printf("[roms] walked %zu native file(s) in %llu ms\n", rels_a.size(), wall_ms_since(t0));

    ScanState state;
    rom_id_scan_begin(state.ids);
    std::vector<RomPairingRow> rows;
    if (!rels_a.empty() && rom_pair_rows(rels_a, "native", &rows)) scan_rows('A', rows, state, out);
    rows.clear();
    std::vector<const char*> roots_b(1, ROM_RETROARCH_ROMS);
    std::string save_root;
    if (rom_retroarch_rows(RETROARCH_PLATFORM, roots_b, &rows, &save_root)) scan_rows('B', rows, state, out);
    rom_id_scan_end(state.ids);
    printf("[roms] scan done: %zu ROM title(s), %d cache hit(s), %d hashed, %d failed in %llu ms\n",
           out.size(), state.ids.hits, state.ids.computed, state.ids.failed, wall_ms_since(t0));
    return out;
}
