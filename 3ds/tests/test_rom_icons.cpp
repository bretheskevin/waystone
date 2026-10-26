// Host-only test for 3DS ROM icon conversion (RGBA8888 -> RGBA5551, icon side by size).
// Box-art decode/downscale is covered by shell-common/tests/test_rom_boxart.cpp.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I 3ds/source -I shell-common \
//     3ds/tests/test_rom_icons.cpp 3ds/source/rom_icons.cpp -o /tmp/test_rom_icons && /tmp/test_rom_icons
#include "rom_icons.h"
#include <cassert>
#include <cstdio>

static void test_rgba8888_to_rgba5551() {
    std::vector<uint8_t> in(ROM_NDS_ICON_RGBA_BYTES, 0), out;
    in[0] = 255; in[3] = 255;
    assert(rgba8888_to_rgba5551(in, NDS_ICON_SIDE, &out));
    assert(out.size() == NDS_ICON_BYTES);
    assert(out[0] == 0x01 && out[1] == 0xF8);
    assert(out[2] == 0x00 && out[3] == 0x00);

    std::vector<uint8_t> grey(BOXART_ICON_SIDE * BOXART_ICON_SIDE * 4, 127);
    for (size_t i = 3; i < grey.size(); i += 4) grey[i] = 255;
    assert(rgba8888_to_rgba5551(grey, BOXART_ICON_SIDE, &out));
    assert(out.size() == BOXART_ICON_BYTES);
    const uint16_t GREY = (uint16_t)((15 << 11) | (15 << 6) | (15 << 1) | 1);
    assert((uint16_t)(out[0] | (out[1] << 8)) == GREY);

    std::vector<uint8_t> bad(7, 0);
    assert(!rgba8888_to_rgba5551(bad, NDS_ICON_SIDE, &out));
    assert(!rgba8888_to_rgba5551(in, BOXART_ICON_SIDE, &out));
    assert(!rgba8888_to_rgba5551(in, 0, &out));
    printf("test_rgba8888_to_rgba5551 PASSED\n");
}

static void test_rom_icon_side() {
    assert(rom_icon_side(NDS_ICON_BYTES) == 32);
    assert(rom_icon_side(BOXART_ICON_BYTES) == BOXART_ICON_SIDE);
    assert(rom_icon_side(0) == 0 && rom_icon_side(123) == 0);
    printf("test_rom_icon_side PASSED\n");
}

int main() {
    test_rgba8888_to_rgba5551();
    test_rom_icon_side();
    printf("ALL rom_icons tests PASSED\n");
    return 0;
}
