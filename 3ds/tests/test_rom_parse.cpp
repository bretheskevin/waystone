// Host-only test for TWiLight ROM parsing helpers.
// Build (from repo root):
//   gcc -O2 -c 3ds/source/stb_image_impl.c -o /tmp/stb_image_impl.o && \
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I 3ds/source \
//     3ds/tests/test_rom_parse.cpp 3ds/source/rom_parse.cpp /tmp/stb_image_impl.o -o /tmp/test_rom_parse
#include "rom_parse.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void test_pair_tsv() {
    const char* tsv = "nds\troms/nds/A.nds\troms/nds/saves\troms/nds/saves/A.sav\troms/nds/saves/A.sav1\n"
                      "gb\troms/gb/B.gb\troms/gb/saves\n";
    std::vector<RomPairingRow> rows;
    assert(parse_rom_pair_tsv(tsv, strlen(tsv), &rows));
    assert(rows.size() == 2);
    assert(rows[0].system == "nds" && rows[0].rom_path == "roms/nds/A.nds");
    assert(rows[0].save_dir == "roms/nds/saves" && rows[0].save_paths.size() == 2);
    assert(rows[1].save_paths.empty());
    std::vector<RomPairingRow> none;
    assert(parse_rom_pair_tsv("", 0, &none) && none.empty());
    assert(!parse_rom_pair_tsv("nds\tonly-two\n", 13, &none));
    printf("test_pair_tsv PASSED\n");
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

static void put_u16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off] = v & 0xFF; b[off + 1] = v >> 8; }

static void test_banner_title() {
    std::vector<uint8_t> banner(NDS_BANNER_BYTES, 0);
    const char* t = "Mario Kart DS\nNintendo";
    for (size_t i = 0; t[i]; i++) put_u16(banner, 0x340 + 2 * i, (uint16_t)t[i]);
    assert(nds_banner_title(banner.data(), banner.size()) == "Mario Kart DS");
    std::vector<uint8_t> b3(NDS_BANNER_BYTES, 0);
    const char* t3 = "Pokemon\nVersion Diamant\nNintendo";
    for (size_t i = 0; t3[i]; i++) put_u16(b3, 0x340 + 2 * i, (uint16_t)t3[i]);
    assert(nds_banner_title(b3.data(), b3.size()) == "Pokemon Version Diamant");
    std::vector<uint8_t> e(NDS_BANNER_BYTES, 0);
    put_u16(e, 0x340, 0x00E9); // e-acute -> 2-byte UTF-8
    assert(nds_banner_title(e.data(), e.size()) == "\xc3\xa9");
    assert(nds_banner_title(banner.data(), 0x100) == "");
    printf("test_banner_title PASSED\n");
}

static void test_banner_icon() {
    std::vector<uint8_t> banner(NDS_BANNER_BYTES, 0);
    put_u16(banner, 0x220 + 2 * 1, 0x001F);       // palette[1] = pure red (BGR555)
    banner[0x20] = 0x10;                           // pixel (0,0)=idx0 (transparent), (1,0)=idx1
    std::vector<uint8_t> px;
    assert(nds_banner_icon_rgba5551(banner.data(), banner.size(), &px));
    assert(px.size() == NDS_ICON_BYTES);
    uint16_t p0 = px[0] | (px[1] << 8), p1 = px[2] | (px[3] << 8);
    assert(p0 == 0x0000);                          // alpha 0
    assert(p1 == ((0x1F << 11) | 1));              // red, alpha 1
    assert(!nds_banner_icon_rgba5551(banner.data(), 0x100, &px));
    printf("test_banner_icon PASSED\n");
}

static void test_misc() {
    std::vector<uint8_t> hdr(0x200, 0);
    hdr[0x68] = 0x00; hdr[0x69] = 0x80; // 0x8000
    assert(nds_banner_offset(hdr.data(), hdr.size()) == 0x8000);
    assert(nds_banner_offset(hdr.data(), 0x40) == 0);
    assert(std::string(rom_system_badge("nds")) == "DS");
    assert(std::string(rom_system_badge("snes")) == "SNES");
    assert(std::string(rom_system_badge("zzz")) == "ROM");
    assert(rom_needs_header("nds") && rom_needs_header("gba") && !rom_needs_header("gb"));
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

static uint16_t px16(const std::vector<uint8_t>& v, int x, int y, int side) {
    size_t o = (size_t)(y * side + x) * 2;
    return (uint16_t)(v[o] | (v[o + 1] << 8));
}

static void test_hidden_names() {
    assert(rom_hidden_name("._Pokemon CHROME.gba"));
    assert(rom_hidden_name(".DS_Store") && rom_hidden_name(".") && rom_hidden_name(".."));
    assert(!rom_hidden_name("Pokemon CHROME.gba") && !rom_hidden_name(""));
    printf("test_hidden_names PASSED\n");
}

static void test_boxart_paths() {
    std::vector<std::string> p = boxart_paths("Pokemon CHROME.gba");
    assert(p.size() == 2);
    assert(p[0] == "_nds/TWiLightMenu/boxart/Pokemon CHROME.gba.bmp");
    assert(p[1] == "_nds/TWiLightMenu/boxart/Pokemon CHROME.gba.png");
    printf("test_boxart_paths PASSED\n");
}

static void test_boxart_dims() {
    assert(boxart_dims_ok(128, 115) && boxart_dims_ok(256, 192) && boxart_dims_ok(1, 1));
    assert(!boxart_dims_ok(257, 100) && !boxart_dims_ok(100, 193));
    assert(!boxart_dims_ok(0, 10) && !boxart_dims_ok(10, -1));
    printf("test_boxart_dims PASSED\n");
}

static void test_boxart_icon_letterbox() {
    std::vector<uint8_t> red(4 * 2 * 4);
    for (size_t i = 0; i < red.size(); i += 4) { red[i] = 0xFF; red[i + 1] = 0; red[i + 2] = 0; red[i + 3] = 0xFF; }
    std::vector<uint8_t> icon;
    assert(boxart_icon_rgba5551(red.data(), 4, 2, &icon));
    assert(icon.size() == BOXART_ICON_BYTES);
    const uint16_t RED = (uint16_t)((0x1F << 11) | 1);
    assert(px16(icon, 0, 11, BOXART_ICON_SIDE) == 0);
    assert(px16(icon, 0, 12, BOXART_ICON_SIDE) == RED);
    assert(px16(icon, 47, 35, BOXART_ICON_SIDE) == RED);
    assert(px16(icon, 47, 36, BOXART_ICON_SIDE) == 0);

    assert(boxart_icon_rgba5551(red.data(), 2, 4, &icon));
    assert(px16(icon, 11, 0, BOXART_ICON_SIDE) == 0 && px16(icon, 12, 0, BOXART_ICON_SIDE) == RED);
    assert(px16(icon, 35, 47, BOXART_ICON_SIDE) == RED && px16(icon, 36, 47, BOXART_ICON_SIDE) == 0);

    assert(!boxart_icon_rgba5551(0, 4, 2, &icon));
    assert(!boxart_icon_rgba5551(red.data(), 0, 2, &icon));
    printf("test_boxart_icon_letterbox PASSED\n");
}

static void test_boxart_icon_box_filter() {
    std::vector<uint8_t> checker(96 * 96 * 4);
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            uint8_t v = ((x + y) % 2) ? 0xFF : 0x00;
            size_t o = (size_t)(y * 96 + x) * 4;
            checker[o] = checker[o + 1] = checker[o + 2] = v;
            checker[o + 3] = 0xFF;
        }
    std::vector<uint8_t> icon;
    assert(boxart_icon_rgba5551(checker.data(), 96, 96, &icon));
    const uint16_t GREY = (uint16_t)((15 << 11) | (15 << 6) | (15 << 1) | 1);
    assert(px16(icon, 0, 0, BOXART_ICON_SIDE) == GREY);
    assert(px16(icon, 47, 47, BOXART_ICON_SIDE) == GREY);

    std::vector<uint8_t> clear(2 * 2 * 4, 0);
    assert(boxart_icon_rgba5551(clear.data(), 2, 2, &icon));
    assert(px16(icon, 10, 10, BOXART_ICON_SIDE) == 0);
    printf("test_boxart_icon_box_filter PASSED\n");
}

static void test_rom_icon_side() {
    assert(rom_icon_side(NDS_ICON_BYTES) == 32);
    assert(rom_icon_side(BOXART_ICON_BYTES) == BOXART_ICON_SIDE);
    assert(rom_icon_side(0) == 0 && rom_icon_side(123) == 0);
    printf("test_rom_icon_side PASSED\n");
}

static const uint8_t PNG_RED_4X2[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x7F,0xA8,0x7D,0x63,0x00,0x00,0x00,0x12,0x49,0x44,0x41,0x54,0x78,0xDA,0x63,0xF8,0xCF,0xC0,0xF0,0x1F,0x19,0x33,0xA0,0x0B,0x00,0x00,0x0F,0x21,0x0F,0xF1,0xFE,0x45,0x14,0x63,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82};
static const uint8_t BMP_RED_4X2[] = {0x42,0x4D,0x4E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x13,0x0B,0x00,0x00,0x13,0x0B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF};
static const uint8_t PNG_WIDE_300X1[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x01,0x2C,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x64,0x38,0x93,0x29,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0xDA,0x63,0x60,0xF8,0x3F,0x0A,0x47,0xE1,0x28,0x1C,0x85,0x43,0x03,0x02,0x00,0x18,0x88,0x55,0xC7,0xD1,0xE2,0x48,0x99,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82};

static void test_boxart_decode() {
    const uint16_t RED = (uint16_t)((0x1F << 11) | 1);
    std::vector<uint8_t> icon;
    int w = 0, h = 0;
    const char* why = 0;
    assert(boxart_decode_icon(PNG_RED_4X2, sizeof(PNG_RED_4X2), &icon, &w, &h, &why));
    assert(w == 4 && h == 2 && icon.size() == BOXART_ICON_BYTES);
    assert(px16(icon, 0, 12, BOXART_ICON_SIDE) == RED && px16(icon, 0, 11, BOXART_ICON_SIDE) == 0);

    assert(boxart_decode_icon(BMP_RED_4X2, sizeof(BMP_RED_4X2), &icon, &w, &h, &why));
    assert(w == 4 && h == 2 && px16(icon, 47, 35, BOXART_ICON_SIDE) == RED);

    assert(!boxart_decode_icon(PNG_WIDE_300X1, sizeof(PNG_WIDE_300X1), &icon, &w, &h, &why));
    assert(why && w == 300 && h == 1);

    const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8};
    why = 0;
    assert(!boxart_decode_icon(junk, sizeof(junk), &icon, &w, &h, &why) && why);
    assert(!boxart_decode_icon(0, 0, &icon, &w, &h, &why) && why);
    printf("test_boxart_decode PASSED\n");
}

int main() {
    test_pair_tsv();
    test_cache_line_round_trip();
    test_banner_title();
    test_banner_icon();
    test_misc();
    test_hidden_names();
    test_boxart_paths();
    test_boxart_dims();
    test_boxart_icon_letterbox();
    test_boxart_icon_box_filter();
    test_rom_icon_side();
    test_boxart_decode();
    printf("ALL rom_parse tests PASSED\n");
    return 0;
}
