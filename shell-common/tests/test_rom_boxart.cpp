// Host-only test for the shared box-art decode + downscale (rom_boxart).
// Build (from repo root):
//   cargo build -p waystone-ffi
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_rom_boxart.cpp shell-common/rom_boxart.cpp shell-common/rom_io.cpp \
//     shell-common/rom_parse.cpp shell-common/snapshot.cpp shell-common/file_tree.cpp \
//     target/debug/libwaystone_ffi.a -liconv -lSystem -lc -lm -o /tmp/test_rom_boxart && /tmp/test_rom_boxart
#include "rom_boxart.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static const RomBoxartLimits LIM = { 256, 192, 256 * 1024 };
static const unsigned SIDE = 48;

static bool px_is(const std::vector<uint8_t>& v, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t* p = &v[((size_t)y * SIDE + (size_t)x) * 4];
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}
static bool red(const std::vector<uint8_t>& v, int x, int y) { return px_is(v, x, y, 255, 0, 0, 255); }
static bool clear(const std::vector<uint8_t>& v, int x, int y) { return px_is(v, x, y, 0, 0, 0, 0); }

static void test_dims() {
    assert(rom_boxart_dims_ok(128, 115, LIM) && rom_boxart_dims_ok(256, 192, LIM) && rom_boxart_dims_ok(1, 1, LIM));
    assert(!rom_boxart_dims_ok(257, 100, LIM) && !rom_boxart_dims_ok(100, 193, LIM));
    assert(!rom_boxart_dims_ok(0, 10, LIM) && !rom_boxart_dims_ok(10, -1, LIM));
}

static void test_fit_letterbox() {
    std::vector<uint8_t> src(4 * 2 * 4);
    for (size_t i = 0; i < src.size(); i += 4) { src[i] = 0xFF; src[i + 1] = 0; src[i + 2] = 0; src[i + 3] = 0xFF; }
    std::vector<uint8_t> out;
    assert(rom_boxart_fit_rgba(src.data(), 4, 2, SIDE, &out));
    assert(out.size() == SIDE * SIDE * 4);
    assert(clear(out, 0, 11) && red(out, 0, 12) && red(out, 47, 35) && clear(out, 47, 36));

    assert(rom_boxart_fit_rgba(src.data(), 2, 4, SIDE, &out));
    assert(clear(out, 11, 0) && red(out, 12, 0) && red(out, 35, 47) && clear(out, 36, 47));

    assert(!rom_boxart_fit_rgba(0, 4, 2, SIDE, &out));
    assert(!rom_boxart_fit_rgba(src.data(), 0, 2, SIDE, &out));
    assert(!rom_boxart_fit_rgba(src.data(), 4, 2, 0, &out));
}

static void test_fit_box_filter() {
    std::vector<uint8_t> checker(96 * 96 * 4);
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            uint8_t v = ((x + y) % 2) ? 0xFF : 0x00;
            size_t o = (size_t)(y * 96 + x) * 4;
            checker[o] = checker[o + 1] = checker[o + 2] = v;
            checker[o + 3] = 0xFF;
        }
    std::vector<uint8_t> out;
    assert(rom_boxart_fit_rgba(checker.data(), 96, 96, SIDE, &out));
    assert(px_is(out, 0, 0, 127, 127, 127, 255) && px_is(out, 47, 47, 127, 127, 127, 255));

    std::vector<uint8_t> transparent(2 * 2 * 4, 0);
    assert(rom_boxart_fit_rgba(transparent.data(), 2, 2, SIDE, &out));
    assert(clear(out, 10, 10));
}

static const uint8_t PNG_RED_4X2[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x7F,0xA8,0x7D,0x63,0x00,0x00,0x00,0x12,0x49,0x44,0x41,0x54,0x78,0xDA,0x63,0xF8,0xCF,0xC0,0xF0,0x1F,0x19,0x33,0xA0,0x0B,0x00,0x00,0x0F,0x21,0x0F,0xF1,0xFE,0x45,0x14,0x63,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82};
static const uint8_t BMP_RED_4X2[] = {0x42,0x4D,0x4E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x13,0x0B,0x00,0x00,0x13,0x0B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0xFF};
static const uint8_t PNG_WIDE_300X1[] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,0x00,0x00,0x01,0x2C,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x64,0x38,0x93,0x29,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0xDA,0x63,0x60,0xF8,0x3F,0x0A,0x47,0xE1,0x28,0x1C,0x85,0x43,0x03,0x02,0x00,0x18,0x88,0x55,0xC7,0xD1,0xE2,0x48,0x99,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82};

static void test_decode() {
    std::vector<uint8_t> out;
    int w = 0, h = 0;
    const char* why = 0;
    assert(rom_boxart_decode(PNG_RED_4X2, sizeof(PNG_RED_4X2), LIM, SIDE, &out, &w, &h, &why));
    assert(w == 4 && h == 2 && out.size() == SIDE * SIDE * 4);
    assert(red(out, 0, 12) && clear(out, 0, 11));

    assert(rom_boxart_decode(BMP_RED_4X2, sizeof(BMP_RED_4X2), LIM, SIDE, &out, &w, &h, &why));
    assert(w == 4 && h == 2 && red(out, 47, 35));

    assert(!rom_boxart_decode(PNG_WIDE_300X1, sizeof(PNG_WIDE_300X1), LIM, SIDE, &out, &w, &h, &why));
    assert(why && w == 300 && h == 1);
    const RomBoxartLimits wide = { 512, 512, 1024 };
    assert(rom_boxart_decode(PNG_WIDE_300X1, sizeof(PNG_WIDE_300X1), wide, SIDE, &out, &w, &h, &why));
    const RomBoxartLimits tiny = { 256, 192, 16 };
    why = 0;
    assert(!rom_boxart_decode(PNG_RED_4X2, sizeof(PNG_RED_4X2), tiny, SIDE, &out, &w, &h, &why) && why);

    const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8};
    why = 0;
    assert(!rom_boxart_decode(junk, sizeof(junk), LIM, SIDE, &out, &w, &h, &why) && why);
    assert(!rom_boxart_decode(0, 0, LIM, SIDE, &out, &w, &h, &why) && why);
}

static void test_load() {
    char tmpl[] = "/tmp/ws_rom_boxart_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir);
    std::string png = std::string(dir) + "/a.png";
    FILE* f = fopen(png.c_str(), "wb");
    assert(f);
    fwrite(PNG_RED_4X2, 1, sizeof(PNG_RED_4X2), f);
    fclose(f);
    std::vector<uint8_t> out;
    assert(rom_boxart_load(png, LIM, SIDE, &out) && red(out, 0, 12));
    const RomBoxartLimits tiny = { 256, 192, 16 };
    assert(!rom_boxart_load(png, tiny, SIDE, &out));
    assert(!rom_boxart_load(std::string(dir) + "/missing.png", LIM, SIDE, &out));
    unlink(png.c_str());
    rmdir(dir);
}

int main() {
    test_dims();
    test_fit_letterbox();
    test_fit_box_filter();
    test_decode();
    test_load();
    printf("test_rom_boxart OK\n");
    return 0;
}
