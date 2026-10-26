#ifndef WAYSTONE_3DS_ROM_ICONS_H
#define WAYSTONE_3DS_ROM_ICONS_H

// 3DS-only ROM icon conversion to RGBA5551: NDS banners + TWiLight Menu++ box art.
#include "rom_parse.h"
#include "rom_art.h"
#include "rom_boxart.h"

static const unsigned NDS_ICON_SIDE = ROM_NDS_ICON_SIDE;
static const size_t NDS_ICON_BYTES   = NDS_ICON_SIDE * NDS_ICON_SIDE * 2;
static const unsigned BOXART_ICON_SIDE = 48;
static const size_t BOXART_ICON_BYTES = BOXART_ICON_SIDE * BOXART_ICON_SIDE * 2;
static const RomBoxartLimits TWILIGHT_BOXART_LIMITS = { 256, 192, 256 * 1024 };  // TWiLight Menu++'s own caps

// RGBA8888 (side^2*4) -> RGBA5551 linear LE (r<<11 | g<<6 | b<<1 | a>=128), lossless for
// core's 5->8 bit expansion. false on a size mismatch.
bool rgba8888_to_rgba5551(const std::vector<uint8_t>& rgba, unsigned side, std::vector<uint8_t>* out);
// Side of a ROM TitleInfo.icon (NDS banner 32, box art BOXART_ICON_SIDE), 0 for unknown sizes.
unsigned rom_icon_side(size_t icon_bytes);

#endif
