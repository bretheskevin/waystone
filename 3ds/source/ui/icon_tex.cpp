#include "icon_tex.h"
#include <cstdio>
#include <cstring>

static const size_t TILE_BYTES = 128;       // 8x8 px * 2 B (RGB565)
static const size_t SRC_TILES_PER_ROW = 6;  // 48/8
static const size_t SRC_TILE_ROWS = 6;
static const size_t DST_TILES_PER_ROW = 8;  // 64/8

IconImage* smdh_icon_to_image(const uint8_t* icon48_rgb565) {
    if (!icon48_rgb565) return nullptr;
    printf("[ui] icon_tex: building 48x48 texture\n");
    IconImage* io = new IconImage();
    memset(io, 0, sizeof(IconImage));
    if (!C3D_TexInit(&io->tex, 64, 64, GPU_RGB565)) {
        printf("[ui] icon_tex: C3D_TexInit failed\n");
        delete io;
        return nullptr;
    }
    C3D_TexSetFilter(&io->tex, GPU_LINEAR, GPU_LINEAR);
    memset(io->tex.data, 0, io->tex.size);
    uint8_t* dst = static_cast<uint8_t*>(io->tex.data);
    const uint8_t* src = icon48_rgb565;
    for (size_t ty = 0; ty < SRC_TILE_ROWS; ty++)
        for (size_t tx = 0; tx < SRC_TILES_PER_ROW; tx++) {
            size_t src_off = (ty * SRC_TILES_PER_ROW + tx) * TILE_BYTES;
            size_t dst_off = (ty * DST_TILES_PER_ROW + tx) * TILE_BYTES;
            memcpy(dst + dst_off, src + src_off, TILE_BYTES);
        }
    io->subtex.width  = 48;
    io->subtex.height = 48;
    io->subtex.left   = 0.0f;
    io->subtex.top    = 1.0f;
    io->subtex.right  = 48.0f / 64.0f;
    io->subtex.bottom = (64.0f - 48.0f) / 64.0f;
    io->img.tex    = &io->tex;
    io->img.subtex = &io->subtex;
    printf("[ui] icon_tex: built 48x48 texture ok\n");
    return io;
}

// PICA200 textures are 8x8 tiles with Morton (Z-order) pixels inside each tile.
static inline unsigned morton8(unsigned x, unsigned y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

IconImage* rgba5551_icon_to_image(const uint8_t* src, unsigned side, bool smooth) {
    if (!src || side == 0 || side > 64) return nullptr;
    unsigned tex = 8;
    while (tex < side) tex <<= 1;
    printf("[ui] icon_tex: building %ux%u RGBA5551 texture (tex %u, %s)\n", side, side, tex,
           smooth ? "linear" : "nearest");
    IconImage* io = new IconImage();
    memset(io, 0, sizeof(IconImage));
    if (!C3D_TexInit(&io->tex, tex, tex, GPU_RGBA5551)) {
        printf("[ui] icon_tex: C3D_TexInit(%ux%u RGBA5551) failed\n", tex, tex);
        delete io;
        return nullptr;
    }
    GPU_TEXTURE_FILTER_PARAM filter = smooth ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&io->tex, filter, filter);
    memset(io->tex.data, 0, io->tex.size);
    uint16_t* dst = static_cast<uint16_t*>(io->tex.data);
    for (unsigned y = 0; y < side; y++)
        for (unsigned x = 0; x < side; x++) {
            size_t o = (y * side + x) * 2;
            dst[((y >> 3) * (tex >> 3) + (x >> 3)) * 64 + morton8(x & 7, y & 7)] =
                (uint16_t)(src[o] | (src[o + 1] << 8));
        }
    C3D_TexFlush(&io->tex);
    io->subtex.width  = (u16)side;
    io->subtex.height = (u16)side;
    io->subtex.left   = 0.0f;
    io->subtex.top    = 1.0f;
    io->subtex.right  = (float)side / (float)tex;
    io->subtex.bottom = 1.0f - (float)side / (float)tex;
    io->img.tex    = &io->tex;
    io->img.subtex = &io->subtex;
    printf("[ui] icon_tex: built %ux%u RGBA5551 texture ok\n", side, side);
    return io;
}

void free_icon_image(IconImage* io) {
    if (!io) return;
    C3D_TexDelete(&io->tex);
    delete io;
}
