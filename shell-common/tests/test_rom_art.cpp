// Host-only test for the shared ROM art helpers (BMP encode, external path map, core banner decode).
// Build (from repo root):
//   cargo build -p waystone-ffi
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_rom_art.cpp shell-common/rom_art.cpp \
//     target/debug/libwaystone_ffi.a -liconv -lSystem -lc -lm -o /tmp/test_rom_art && /tmp/test_rom_art
#include "rom_art.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static uint32_t le32(const std::vector<uint8_t>& v, size_t o) {
    return (uint32_t)v[o] | ((uint32_t)v[o + 1] << 8) | ((uint32_t)v[o + 2] << 16) | ((uint32_t)v[o + 3] << 24);
}

static void test_bmp() {
    const uint8_t rgba[16] = {
        1, 2, 3, 4,     5, 6, 7, 8,
        9, 10, 11, 12,  13, 14, 15, 16,
    };
    std::vector<uint8_t> bmp = rom_encode_bmp_rgba(rgba, 2, 2);
    assert(bmp.size() == 54 + 16);
    assert(bmp[0] == 'B' && bmp[1] == 'M');
    assert(le32(bmp, 2) == bmp.size());
    assert(le32(bmp, 10) == 54);
    assert(le32(bmp, 18) == 2 && le32(bmp, 22) == 2);
    assert(bmp[28] == 32);
    // pixel (0,0) goes to the last row, BGRA
    assert(bmp[54 + 8] == 3 && bmp[54 + 9] == 2 && bmp[54 + 10] == 1 && bmp[54 + 11] == 4);
    assert(bmp[54] == 11 && bmp[54 + 3] == 12);
    assert(rom_encode_bmp_rgba(0, 2, 2).empty());
    assert(rom_encode_bmp_rgba(rgba, 100000, 100000).empty());  // size overflow rejected
}

static void test_external_rel() {
    assert(rom_external_rel("@saves/Zelda.srm", "retroarch/saves") == "retroarch/saves/Zelda.srm");
    assert(rom_external_rel("@saves", "retroarch/saves") == "retroarch/saves");
    assert(rom_external_rel("roms/x", "r") == "roms/x");
}

static void test_decode_banner() {
    std::vector<uint8_t> b(ROM_NDS_BANNER_BYTES, 0);
    b[0x222] = 0x1F;
    b[0x20] = 0x01;
    const char* t = "Mario Kart DS\nNintendo";
    for (size_t i = 0; t[i]; i++) b[0x340 + 2 * i] = (uint8_t)t[i];
    std::vector<uint8_t> rgba;
    std::string title;
    assert(rom_decode_nds_banner(b, &rgba, &title));
    assert(rgba.size() == ROM_NDS_ICON_RGBA_BYTES);
    assert(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255);
    assert(title == "Mario Kart DS");
    std::vector<uint8_t> shortb(0x100, 0);
    assert(!rom_decode_nds_banner(shortb, &rgba, &title));
}

static void test_boxart_rules() {
    std::vector<std::string> p = rom_twilight_boxart_paths("Zelda.gba");
    assert(p.size() == 2);
    assert(p[0] == "_nds/TWiLightMenu/boxart/Zelda.gba.bmp");
    assert(p[1] == "_nds/TWiLightMenu/boxart/Zelda.gba.png");
    assert(rom_retroarch_boxart_rel("gba", "A&B.gba") ==
           "thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/A_B.png");
    assert(rom_retroarch_boxart_rel("switch", "x").empty());
}

static void test_display_name() {
    assert(rom_display_name("gba", std::vector<uint8_t>(), "Zelda (U).gba") == "Zelda (U)");
}

int main() {
    test_bmp();
    test_external_rel();
    test_decode_banner();
    test_boxart_rules();
    test_display_name();
    printf("test_rom_art OK\n");
    return 0;
}
