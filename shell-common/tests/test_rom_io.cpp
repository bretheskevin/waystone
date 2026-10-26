// Host-only test for the shared ROM IO helpers (extension, cue resolution, walk).
// Build (from repo root):
//   cargo build -p waystone-ffi
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_rom_io.cpp shell-common/rom_io.cpp shell-common/rom_parse.cpp \
//     shell-common/snapshot.cpp shell-common/file_tree.cpp \
//     target/debug/libwaystone_ffi.a -liconv -lSystem -lc -lm -o /tmp/test_rom_io && /tmp/test_rom_io
#include "rom_io.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static void write_text(const std::string& path, const std::string& text) {
    FILE* f = fopen(path.c_str(), "wb");
    assert(f);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

static void test_lower_ext() {
    assert(rom_lower_ext("Game.CUE") == "cue");
    assert(rom_lower_ext("a.b.Nds") == "nds");
    assert(rom_lower_ext("noext") == "");
}

static void test_resolve_cue() {
    char tmpl[] = "/tmp/ws_rom_io_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir);
    std::string d = dir;
    std::string out;

    write_text(d + "/ok.cue", "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
    assert(rom_resolve_cue(d + "/ok.cue", &out));
    assert(out == d + "/disc.bin");

    write_text(d + "/up.cue", "FILE \"../x.bin\" BINARY\n  TRACK 01 MODE2/2352\n");
    assert(!rom_resolve_cue(d + "/up.cue", &out));

    write_text(d + "/abs.cue", "FILE \"/abs.bin\" BINARY\n  TRACK 01 MODE2/2352\n");
    assert(!rom_resolve_cue(d + "/abs.cue", &out));

    assert(!rom_resolve_cue(d + "/missing.cue", &out));
}

static void test_pair_rows() {
    std::vector<std::string> rels;
    rels.push_back("roms/gba/Zelda.gba");
    rels.push_back("roms/gba/Zelda.sav");
    std::vector<RomPairingRow> rows;
    assert(rom_pair_rows(rels, "native", &rows));
    assert(rows.size() == 1);
    assert(rows[0].system == "gba" && rows[0].rom_path == "roms/gba/Zelda.gba");
    assert(rows[0].save_paths.size() == 1);
}

static void test_base_name() {
    assert(rom_base_name("sdmc:/roms/gba/Zelda.gba") == "Zelda.gba");
    assert(rom_base_name("Zelda.gba") == "Zelda.gba");
}

static void test_write_then_extract() {
    char tmpl[] = "/tmp/ws_rom_saves_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir);
    std::string d = std::string(dir) + "/sub";
    std::vector<FileTreeEntry> entries;
    entries.push_back(FileTreeEntry("Zelda.sav", std::vector<uint8_t>(3, 7)));
    assert(rom_write_saves(d, entries) == 0);
    entries.push_back(FileTreeEntry("../evil.sav", std::vector<uint8_t>(1, 1)));
    assert(rom_write_saves(d, entries) == -1);
    assert(rom_write_saves("", entries) == -1);

    std::vector<std::string> paths(1, d + "/Zelda.sav");
    std::vector<uint8_t> tree = rom_extract_saves("gba", paths);
    std::vector<FileTreeEntry> back;
    assert(file_tree_decode(tree.data(), tree.size(), &back));
    assert(back.size() == 1 && back[0].first == "gba/rom/Zelda.sav" && back[0].second.size() == 3);
    paths.push_back(d + "/missing.sav");
    assert(rom_extract_saves("gba", paths).empty());
}

int main() {
    test_lower_ext();
    test_base_name();
    test_write_then_extract();
    test_resolve_cue();
    test_pair_rows();
    printf("test_rom_io OK\n");
    return 0;
}
