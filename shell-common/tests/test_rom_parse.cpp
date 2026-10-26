// Host-only test for the shared ROM pairing/cache parse helpers.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common \
//     shell-common/tests/test_rom_parse.cpp shell-common/rom_parse.cpp -o /tmp/test_rom_parse \
//     && /tmp/test_rom_parse
#include "rom_parse.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void test_pair_tsv() {
    const char* tsv = "nds\troms/nds/A.nds\troms/nds/saves\troms/nds/saves/A.sav\troms/nds/saves/A.sav1\n"
                      "gb\troms/gb/B.gb\troms/gb/saves\n";
    std::vector<RomPairingRow> rows;
    assert(parse_rom_pair_tsv(tsv, strlen(tsv), false, "native", &rows));
    assert(rows.size() == 2);
    assert(rows[0].flavor == "native" && rows[1].flavor == "native");
    assert(rows[0].system == "nds" && rows[0].rom_path == "roms/nds/A.nds");
    assert(rows[0].save_dir == "roms/nds/saves" && rows[0].save_paths.size() == 2);
    assert(rows[1].save_paths.empty());
    std::vector<RomPairingRow> none;
    assert(parse_rom_pair_tsv("", 0, false, "native", &none) && none.empty());
    assert(!parse_rom_pair_tsv("nds\tonly-two\n", 13, false, "native", &none));
    printf("test_pair_tsv PASSED\n");
}

static void test_pair_tsv_leading_flavor() {
    const char* tsv = "retroarch\tgba\troms/gba/A.gba\t@saves/mGBA\t@saves/mGBA/A.srm\n"
                      "native\tgba\troms/gba/B.gba\troms/gba\troms/gba/B.sav\n";
    std::vector<RomPairingRow> rows;
    assert(parse_rom_pair_tsv(tsv, strlen(tsv), true, "retroarch", &rows));
    assert(rows.size() == 2);
    assert(rows[0].flavor == "retroarch" && rows[0].system == "gba" && rows[0].rom_path == "roms/gba/A.gba");
    assert(rows[0].save_dir == "@saves/mGBA" && rows[0].save_paths.size() == 1);
    assert(rows[1].flavor == "native" && rows[1].save_paths[0] == "roms/gba/B.sav");
    const char* short_row = "retroarch\tgba\troms/gba/A.gba\n";
    assert(!parse_rom_pair_tsv(short_row, strlen(short_row), true, "retroarch", &rows));
    const char* no_flavor = "\tgba\troms/gba/A.gba\troms/gba\n";
    assert(!parse_rom_pair_tsv(no_flavor, strlen(no_flavor), true, "retroarch", &rows));
    printf("test_pair_tsv_leading_flavor PASSED\n");
}

static void test_join() {
    std::vector<std::string> items;
    assert(rom_join(items, '\n') == "");
    items.push_back("corename = \"mGBA\"\n");
    assert(rom_join(items, '\x1e') == "corename = \"mGBA\"\n");
    items.push_back("corename = \"melonDS\"\n");
    assert(rom_join(items, '\x1e') == "corename = \"mGBA\"\n\x1e" "corename = \"melonDS\"\n");
    printf("test_join PASSED\n");
}

static void test_cache_line_round_trip() {
    RomIdCacheEntry e;
    e.path = "roms/nds/A b.nds"; e.size = 33554432ULL; e.mtime = 1700000000LL;
    e.system = "nds"; e.rom_id = "AMCE-1A2B";
    std::string line = format_rom_id_cache_line(e);
    assert(line == "roms/nds/A b.nds\t33554432\t1700000000\tnds\tAMCE-1A2B");
    RomIdCacheEntry back;
    assert(parse_rom_id_cache_line(line, &back));
    assert(back.path == e.path && back.size == e.size && back.mtime == e.mtime);
    assert(back.system == e.system && back.rom_id == e.rom_id);
    assert(!parse_rom_id_cache_line("garbage", &back));
    assert(!parse_rom_id_cache_line("p\tNaN\t1\tnds\tX", &back));
    printf("test_cache_line_round_trip PASSED\n");
}

static void test_misc() {
    assert(std::string(rom_system_badge("nds")) == "DS");
    assert(std::string(rom_system_badge("snes")) == "SNES");
    assert(std::string(rom_system_badge("zzz")) == "ROM");
    assert(strcmp(rom_system_badge("n64"), "N64") == 0);
    assert(strcmp(rom_system_badge("psx"), "PSX") == 0);
    assert(rom_needs_header("nds") && rom_needs_header("gba") && !rom_needs_header("gb"));
    assert(rom_needs_header("n64"));
    assert(!rom_needs_header("psx"));
    uint32_t a = rom_synthetic_unique_id("nds", "AMCE-1A2B");
    assert((a & 0x80000000u) && a == rom_synthetic_unique_id("nds", "AMCE-1A2B"));
    assert(a != rom_synthetic_unique_id("gba", "AMCE-1A2B"));
    assert(rom_group_key("nds", "AMCE-1A2B", "battery") == "nds/AMCE-1A2B/battery");
    assert(rom_group_key("gb", "CBF43926", 0) == "gb/CBF43926");
    assert(rom_save_name_safe("Mario.sav") && !rom_save_name_safe("../x") && !rom_save_name_safe("a/b"));
    assert(!rom_save_name_safe("") && !rom_save_name_safe(".."));
    assert(title_name_less("apple", "Banana") && !title_name_less("banana", "Apple"));
    printf("test_misc PASSED\n");
}

static void test_hidden_names() {
    assert(rom_hidden_name("._Pokemon CHROME.gba"));
    assert(rom_hidden_name(".DS_Store") && rom_hidden_name(".") && rom_hidden_name(".."));
    assert(!rom_hidden_name("Pokemon CHROME.gba") && !rom_hidden_name(""));
    printf("test_hidden_names PASSED\n");
}

int main() {
    test_pair_tsv();
    test_pair_tsv_leading_flavor();
    test_join();
    test_cache_line_round_trip();
    test_misc();
    test_hidden_names();
    printf("ALL rom_parse tests PASSED\n");
    return 0;
}
