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

void free_icon_image(IconImage* io) {
    if (!io) return;
    C3D_TexDelete(&io->tex);
    delete io;
}
