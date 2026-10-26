// Host-only test for the shared RetroArch save-layout scan helper.
// Build (from repo root):
//   cargo build -p waystone-ffi
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_rom_retroarch.cpp shell-common/rom_retroarch.cpp shell-common/rom_io.cpp \
//     shell-common/rom_parse.cpp shell-common/rom_art.cpp shell-common/snapshot.cpp shell-common/file_tree.cpp \
//     target/debug/libwaystone_ffi.a -liconv -lSystem -lc -lm -o /tmp/test_rom_retroarch && /tmp/test_rom_retroarch
#include "rom_retroarch.h"
#include "snapshot.h"  // fs_mkdir_p
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const std::string& path, const std::string& text) {
    std::string dir = path.substr(0, path.rfind('/'));
    assert(fs_mkdir_p(dir));
    FILE* f = fopen(path.c_str(), "wb");
    assert(f);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

static void test_core_info_rel() {
    assert(rom_core_info_rel("mgba_libretro_libnx.nro", "retroarch/info") == "retroarch/info/mgba_libretro.info");
    assert(rom_core_info_rel("melonds_libretro.so", "retroarch/info") == "retroarch/info/melonds_libretro.info");
    assert(rom_core_info_rel("retroarch_switch.nro", "retroarch/info") == "");
    assert(rom_core_info_rel("_libretro.nro", "retroarch/info") == "");
}

static void test_external_prefix() {
    std::vector<std::string> walked;
    walked.push_back("retroarch/cores/savefiles/Zelda.srm");
    walked.push_back("retroarch/cores/savefiles/mGBA/Pokemon CHROME.srm");
    walked.push_back("retroarch/cores/savefiles/mGBA/gba/Deep.srm");
    std::vector<std::string> out(1, "roms/gba/Zelda.gba");
    rom_append_external(walked, "retroarch/cores/savefiles", &out);
    assert(out.size() == 4);
    assert(out[0] == "roms/gba/Zelda.gba");
    assert(out[1] == "@saves/Zelda.srm");
    assert(out[2] == "@saves/mGBA/Pokemon CHROME.srm");
    assert(out[3] == "@saves/mGBA/gba/Deep.srm");
}

static void test_dirs() {
    RomRetroarchDirs d;
    const std::string cfg = "savefile_directory = \"/retroarch/cores/savefiles\"\n"
                            "sort_savefiles_enable = \"true\"\n";
    assert(rom_retroarch_dirs(cfg, "switch", &d));
    assert(d.save_root == "retroarch/cores/savefiles");
    assert(d.cores_dir == "retroarch/cores" && d.info_dir == "retroarch/info");
    assert(rom_retroarch_dirs("", "switch", &d));
    assert(d.save_root == "retroarch/cores/savefiles" && d.info_dir == "retroarch/info");
    assert(rom_retroarch_dirs("", "3ds", &d));
    assert(d.save_root == "retroarch/cores/savefiles" && d.info_dir == "retroarch/cores/info");
    assert(rom_retroarch_dirs("savefiles_in_content_dir = \"true\"", "3ds", &d));
    assert(d.save_root.empty());
    assert(!rom_retroarch_dirs("", "wii", &d));
}

static const char* const ROOTS[] = {"roms", "retroarch/roms"};

static void test_rows_user_layout() {
    write_text("sdmc:/retroarch/retroarch.cfg",
               "savefile_directory = \"/retroarch/cores/savefiles\"\n"
               "savefiles_in_content_dir = \"false\"\nsort_savefiles_enable = \"true\"\n");
    write_text("sdmc:/retroarch/cores/mgba_libretro_libnx.nro", "x");
    write_text("sdmc:/retroarch/cores/melonds_libretro_libnx.nro", "x");
    write_text("sdmc:/retroarch/cores/readme.txt", "x");
    write_text("sdmc:/retroarch/info/mgba_libretro.info", "corename = \"mGBA\"\nsupported_extensions = \"gba|gb\"\n");
    write_text("sdmc:/retroarch/info/melonds_libretro.info", "corename = \"melonDS\"\nsupported_extensions = \"nds\"\n");
    write_text("sdmc:/roms/gba/Pokemon CHROME.gba", "rom");
    write_text("sdmc:/roms/nds/Mario.nds", "rom");
    write_text("sdmc:/retroarch/cores/savefiles/mGBA/Pokemon CHROME.srm", "save");
    write_text("sdmc:/retroarch/cores/savefiles/mGBA/._Pokemon CHROME.srm", "junk");

    std::vector<const char*> roots(ROOTS, ROOTS + 2);
    std::vector<RomPairingRow> rows;
    std::string save_root;
    assert(rom_retroarch_rows("switch", roots, &rows, &save_root));
    assert(save_root == "retroarch/cores/savefiles");
    assert(rows.size() == 2);
    assert(rows[0].flavor == "retroarch" && rows[1].flavor == "retroarch");
    assert(rows[0].system == "gba" && rows[0].rom_path == "roms/gba/Pokemon CHROME.gba");
    assert(rows[0].save_dir == "retroarch/cores/savefiles/mGBA");
    assert(rows[0].save_paths.size() == 1);
    assert(rows[0].save_paths[0] == "retroarch/cores/savefiles/mGBA/Pokemon CHROME.srm");
    assert(rows[1].system == "nds" && rows[1].rom_path == "roms/nds/Mario.nds");
    assert(rows[1].save_dir == "retroarch/cores/savefiles/melonDS");
    assert(rows[1].save_paths.empty());
}

static void test_rows_default_dirs_3ds() {
    write_text("sdmc:/retroarch/retroarch.cfg", "savefile_directory = \"default\"\nsort_savefiles_enable = \"true\"\n");
    std::vector<const char*> roots(ROOTS, ROOTS + 2);
    std::vector<RomPairingRow> rows;
    std::string save_root;
    assert(rom_retroarch_rows("3ds", roots, &rows, &save_root));
    assert(save_root == "retroarch/cores/savefiles");
    assert(rows.size() == 2);
    assert(rows[0].rom_path == "roms/gba/Pokemon CHROME.gba" && rows[0].flavor == "retroarch");
    assert(rows[0].save_paths.size() == 1 && rows[0].save_dir == "retroarch/cores/savefiles/mGBA");
    // 3DS reads core infos from retroarch/cores/info (empty here) -> no per-core restore folder.
    assert(rows[1].rom_path == "roms/nds/Mario.nds" && rows[1].save_dir == "retroarch/cores/savefiles");
    write_text("sdmc:/retroarch/cores/info/melonds_libretro.info",
               "corename = \"melonDS\"\nsupported_extensions = \"nds\"\n");
    assert(rom_retroarch_rows("3ds", roots, &rows, &save_root));
    assert(rows[1].save_dir == "retroarch/cores/savefiles/melonDS");
    assert(remove("sdmc:/retroarch/retroarch.cfg") == 0);
    assert(rom_retroarch_rows("3ds", roots, &rows, &save_root));
    assert(save_root == "retroarch/cores/savefiles");
    assert(rows[1].save_dir == "retroarch/cores/savefiles/melonDS");  // no cfg: per-core sorting is RetroArch's default
}

static void test_rows_content_dir_native_fallback() {
    write_text("sdmc:/retroarch/retroarch.cfg", "savefiles_in_content_dir = \"true\"\n");
    write_text("sdmc:/roms/gba/Zelda.gba", "rom");
    write_text("sdmc:/roms/gba/Zelda.sav", "save");
    std::vector<const char*> roots(ROOTS, ROOTS + 2);
    std::vector<RomPairingRow> rows;
    std::string save_root = "stale";
    assert(rom_retroarch_rows("switch", roots, &rows, &save_root));
    assert(save_root.empty());
    bool zelda = false;
    for (size_t i = 0; i < rows.size(); i++) {
        assert(rows[i].save_dir.compare(0, 7, "@saves") != 0);
        if (rows[i].rom_path == "roms/gba/Pokemon CHROME.gba") {
            assert(rows[i].save_dir == "roms/gba/mGBA" && rows[i].flavor == "retroarch");
        }
        if (rows[i].rom_path == "roms/gba/Zelda.gba") {
            zelda = true;
            assert(rows[i].flavor == "native");
            assert(rows[i].save_paths.size() == 1 && rows[i].save_paths[0] == "roms/gba/Zelda.sav");
        }
    }
    assert(zelda);
}

int main() {
    test_core_info_rel();
    test_external_prefix();
    test_dirs();
    char tmpl[] = "/tmp/ws_rom_ra_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir && chdir(dir) == 0);
    assert(mkdir("sdmc:", 0755) == 0);  // host stand-in for the SD root ("sdmc:/x" is relative)
    test_rows_user_layout();
    test_rows_default_dirs_3ds();
    test_rows_content_dir_native_fallback();
    printf("test_rom_retroarch: all passed\n");
    return 0;
}
