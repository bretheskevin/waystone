#include "rom_icons.h"

bool rgba8888_to_rgba5551(const std::vector<uint8_t>& rgba, unsigned side, std::vector<uint8_t>* out) {
    const size_t pixels = (size_t)side * side;
    if (pixels == 0 || rgba.size() != pixels * 4) return false;
    out->assign(pixels * 2, 0);
    for (size_t i = 0; i < pixels; i++) {
        const uint8_t* p = &rgba[i * 4];
        uint16_t px = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) |
                                 (p[3] >= 128 ? 1 : 0));
        (*out)[i * 2] = (uint8_t)(px & 0xFF);
        (*out)[i * 2 + 1] = (uint8_t)(px >> 8);
    }
    return true;
}

unsigned rom_icon_side(size_t icon_bytes) {
    if (icon_bytes == NDS_ICON_BYTES) return NDS_ICON_SIDE;
    if (icon_bytes == BOXART_ICON_BYTES) return BOXART_ICON_SIDE;
    return 0;
}
