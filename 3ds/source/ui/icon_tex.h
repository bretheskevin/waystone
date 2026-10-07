#pragma once
#include <citro3d.h>
#include <citro2d.h>
#include <cstdint>

struct IconImage {
    C3D_Tex tex;
    Tex3DS_SubTexture subtex;
    C2D_Image img;       // img.tex = &tex; img.subtex = &subtex
};

// Build a C2D_Image from raw 48x48 RGB565 tiled SMDH icon bytes (4608 B).
// Returns heap-allocated IconImage* (caller owns), or nullptr on failure.
// MUST be called on the render thread only (C3D_TexInit touches the GPU).
IconImage* smdh_icon_to_image(const uint8_t* icon48_rgb565);

// Build a C2D_Image from a side x side RGBA5551 linear row-major (little-endian) icon: NDS banner
// (32, nearest filter) or TWiLight box art (48, smooth). side <= 64.
// Render thread only. Returns heap IconImage* (free with free_icon_image) or nullptr.
IconImage* rgba5551_icon_to_image(const uint8_t* rgba5551, unsigned side, bool smooth);

// Free the GPU texture and delete the struct. Safe to call with nullptr.
void free_icon_image(IconImage* io);
