#include "rom_retroarch.h"
#include "rom_io.h"
#include "rom_art.h"
#include "ffi_err.h"
#include "wall_clock.h"

#include <cstdio>

static const char INFO_SEP = '\x1e';
static const char LIBRETRO_TAG[] = "_libretro";

bool rom_retroarch_dirs(const std::string& cfg_text, const char* platform, RomRetroarchDirs* out) {
    WsBuf b = ws_retroarch_dirs(reinterpret_cast<const uint8_t*>(cfg_text.data()), cfg_text.size(), platform);
    if (!b.ptr) {
        printf("[roms] ws_retroarch_dirs (platform=%s) failed: %s\n", platform ? platform : "(null)", ffi_err());
        return false;
    }
    std::string s(reinterpret_cast<const char*>(b.ptr), b.len);
    ws_buf_free(b);
    size_t t1 = s.find('\t');
    size_t t2 = t1 == std::string::npos ? t1 : s.find('\t', t1 + 1);
    if (t2 == std::string::npos) { printf("[roms] malformed retroarch dirs '%s'\n", s.c_str()); return false; }
    out->save_root = s.substr(0, t1);
    out->cores_dir = s.substr(t1 + 1, t2 - t1 - 1);
    out->info_dir = s.substr(t2 + 1);
    return true;
}

std::string rom_core_info_rel(const std::string& core_file_name, const std::string& info_dir) {
    size_t at = core_file_name.find(LIBRETRO_TAG);
    if (at == std::string::npos || at == 0) return std::string();
    return info_dir + "/" + core_file_name.substr(0, at + sizeof(LIBRETRO_TAG) - 1) + ".info";
}

void rom_append_external(const std::vector<std::string>& walked, const std::string& root,
                         std::vector<std::string>* out) {
    for (size_t i = 0; i < walked.size(); i++)
        out->push_back(ROM_EXTERNAL_SAVES + walked[i].substr(root.size()));
}

static std::string read_text(const std::string& rel) {
    std::vector<uint8_t> bytes;
    if (!rom_read_whole(rom_sd_abs(rel), &bytes)) return std::string();
    return std::string(bytes.begin(), bytes.end());
}

// Installed cores = files in cores_dir containing "_libretro"; returns their readable .info texts.
static std::vector<std::string> installed_core_infos(const RomRetroarchDirs& dirs) {
    std::vector<std::string> cores, texts;
    rom_walk_rel(dirs.cores_dir, 0, 0, cores);
    for (size_t i = 0; i < cores.size(); i++) {
        std::string info = rom_core_info_rel(rom_base_name(cores[i]), dirs.info_dir);
        if (info.empty()) continue;
        std::string text = read_text(info);
        if (text.empty()) { printf("[roms] core %s: info %s unreadable (skipped)\n", cores[i].c_str(), info.c_str()); continue; }
        printf("[roms] core %s -> %s\n", rom_base_name(cores[i]).c_str(), info.c_str());
        texts.push_back(text);
    }
    printf("[roms] %zu installed core info(s) in %s\n", texts.size(), dirs.cores_dir.c_str());
    return texts;
}

bool rom_retroarch_rows(const char* platform, const std::vector<const char*>& rom_roots,
                        std::vector<RomPairingRow>* rows, std::string* save_root_rel) {
    unsigned long long t0 = wall_now_ms();
    std::string cfg = read_text(ROM_RETROARCH_CFG);
    printf("[roms] retroarch cfg %s: %s (%zu bytes)\n", ROM_RETROARCH_CFG, cfg.empty() ? "missing/empty" : "found",
           cfg.size());
    RomRetroarchDirs dirs;
    if (!rom_retroarch_dirs(cfg, platform, &dirs)) return false;
    printf("[roms] retroarch dirs (platform=%s): saves='%s'%s cores='%s' info='%s'\n", platform, dirs.save_root.c_str(),
           dirs.save_root.empty() ? " (content dir)" : "", dirs.cores_dir.c_str(), dirs.info_dir.c_str());
    *save_root_rel = dirs.save_root;
    std::string infos = rom_join(installed_core_infos(dirs), INFO_SEP);

    std::vector<std::string> paths;
    for (size_t i = 0; i < rom_roots.size(); i++) rom_walk_rel(rom_roots[i], 0, ROM_SCAN_MAX_DEPTH, paths);
    size_t rom_files = paths.size();
    if (!dirs.save_root.empty()) {
        std::vector<std::string> saves;
        rom_walk_rel(dirs.save_root, 0, ROM_RETROARCH_SAVE_DEPTH, saves);
        rom_append_external(saves, dirs.save_root, &paths);
    }
    printf("[roms] retroarch walked %zu ROM-tree + %zu save-root file(s) in %llu ms\n", rom_files,
           paths.size() - rom_files, wall_ms_since(t0));
    if (rom_files == 0) return true;

    std::string joined = rom_join(paths, '\n');
    WsBuf pairs = ws_rom_pair_retroarch(reinterpret_cast<const uint8_t*>(joined.data()), joined.size(),
                                        reinterpret_cast<const uint8_t*>(cfg.data()), cfg.size(),
                                        reinterpret_cast<const uint8_t*>(infos.data()), infos.size(), platform);
    if (!rom_pair_rows_from_buf(pairs, "retroarch", true, rows)) return false;
    for (size_t i = 0; i < rows->size(); i++) {
        RomPairingRow& r = (*rows)[i];
        r.save_dir = rom_external_rel(r.save_dir, dirs.save_root);
        for (size_t s = 0; s < r.save_paths.size(); s++)
            r.save_paths[s] = rom_external_rel(r.save_paths[s], dirs.save_root);
    }
    return true;
}
