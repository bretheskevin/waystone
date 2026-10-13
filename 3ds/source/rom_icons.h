#ifndef WAYSTONE_3DS_ROM_ICONS_H
#define WAYSTONE_3DS_ROM_ICONS_H

// 3DS-only ROM icon decoding: NDS banner + TWiLight Menu++ box art (vendored stb_image).
#include "rom_parse.h"

static const size_t NDS_BANNER_BYTES = 0x840;
static const unsigned NDS_ICON_SIDE = 32;
static const size_t NDS_ICON_BYTES   = NDS_ICON_SIDE * NDS_ICON_SIDE * 2;
static const unsigned BOXART_ICON_SIDE = 48;
static const size_t BOXART_ICON_BYTES = BOXART_ICON_SIDE * BOXART_ICON_SIDE * 2;
static const int BOXART_MAX_W = 256;                    // TWiLight Menu++'s own box-art limit
static const int BOXART_MAX_H = 192;
static const size_t BOXART_MAX_FILE_BYTES = 256 * 1024;

// NDS header 0x68 banner offset; 0 when absent or header too short.
uint32_t nds_banner_offset(const uint8_t* header, size_t len);
// English banner title (0x340): all lines but the last (publisher) joined by ' '; "" if unavailable.
std::string nds_banner_title(const uint8_t* banner, size_t len);
// 32x32 icon -> RGBA5551 linear row-major little-endian (NDS_ICON_BYTES); palette index 0 transparent.
bool nds_banner_icon_rgba5551(const uint8_t* banner, size_t len, std::vector<uint8_t>* out);

std::string utf16le_to_utf8(const uint8_t* p, size_t max_units);

// TWiLight Menu++ box art for a non-NDS ROM, SD-root-relative, in TWiLight's lookup order.
std::vector<std::string> boxart_paths(const std::string& rom_file_name);
bool boxart_dims_ok(int w, int h);
// RGBA8888 (w*h*4) -> BOXART_ICON_SIDE^2 RGBA5551 linear LE: aspect-fit, centred, box-filtered,
// transparent padding.
bool boxart_icon_rgba5551(const uint8_t* rgba, int w, int h, std::vector<uint8_t>* out);
// Decode box-art file bytes (PNG/BMP) via stb_image into a BOXART_ICON_BYTES icon. *w/*h = source
// size when the header parsed; on failure *why is a static reason string.
bool boxart_decode_icon(const uint8_t* data, size_t len, std::vector<uint8_t>* icon,
                        int* w, int* h, const char** why);
// Side of a ROM TitleInfo.icon (NDS banner 32, box art BOXART_ICON_SIDE), 0 for unknown sizes.
unsigned rom_icon_side(size_t icon_bytes);

#endif
