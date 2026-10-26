#ifndef WAYSTONE_ROM_BOXART_H
#define WAYSTONE_ROM_BOXART_H

// Box-art decode + downscale shared by the Switch and 3DS ROM scans (runs on the scan worker).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RomBoxartLimits {
    int max_w;
    int max_h;
    size_t max_file_bytes;
};

bool rom_boxart_dims_ok(int w, int h, const RomBoxartLimits& lim);
// RGBA8888 (w*h*4) -> side*side RGBA8888: aspect-fit, centred, alpha-weighted box filter,
// transparent padding.
bool rom_boxart_fit_rgba(const uint8_t* rgba, int w, int h, unsigned side, std::vector<uint8_t>* out);
// Decode PNG/BMP bytes (stb_image) and fit them into a side*side RGBA8888 icon. *w/*h = source size
// once the header parsed; on failure *why is a static reason string.
bool rom_boxart_decode(const uint8_t* data, size_t len, const RomBoxartLimits& lim, unsigned side,
                       std::vector<uint8_t>* rgba, int* w, int* h, const char** why);
// Stat (size cap) + read + rom_boxart_decode of the box-art file at abs; [roms] logs. false when
// absent, oversized or undecodable.
bool rom_boxart_load(const std::string& abs, const RomBoxartLimits& lim, unsigned side,
                     std::vector<uint8_t>* rgba);

#endif
