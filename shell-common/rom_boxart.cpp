#include "rom_boxart.h"
#include "rom_io.h"  // rom_read_whole
#include "wall_clock.h"

#include <cstdio>
#include <sys/stat.h>

// The Switch GUI links borealis nanovg-deko3d, which already compiles an extern stb_image (its
// include dir is only on the GUI build's path), so use its symbols there. Every other build
// (3DS, Switch CONSOLE=1, host tests) gets a TU-local PNG+BMP-only copy: no duplicate symbols.
#if defined(__SWITCH__) && defined(__has_include)
#if __has_include(<nanovg/stb_image.h>)
#define WS_STB_FROM_NANOVG
#endif
#endif

#ifdef WS_STB_FROM_NANOVG
#include <nanovg/stb_image.h>
#else
// vendor/stb_image.h: v2.30 from nothings/stb@2c980bb5 (MIT / public domain), unmodified.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "vendor/stb_image.h"
#pragma GCC diagnostic pop
#endif

bool rom_boxart_dims_ok(int w, int h, const RomBoxartLimits& lim) {
    return w > 0 && h > 0 && w <= lim.max_w && h <= lim.max_h;
}

bool rom_boxart_fit_rgba(const uint8_t* rgba, int w, int h, unsigned side, std::vector<uint8_t>* out) {
    if (!rgba || w <= 0 || h <= 0 || side == 0) return false;
    const int s = (int)side;
    int dw = s, dh = s;
    if (w >= h) dh = (h * s + w / 2) / w;
    else dw = (w * s + h / 2) / h;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    const int ox = (s - dw) / 2, oy = (s - dh) / 2;
    out->assign((size_t)s * (size_t)s * 4, 0);
    for (int dy = 0; dy < dh; dy++) {
        int y0 = dy * h / dh, y1 = (dy + 1) * h / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int dx = 0; dx < dw; dx++) {
            int x0 = dx * w / dw, x1 = (dx + 1) * w / dw;
            if (x1 <= x0) x1 = x0 + 1;
            uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    const uint8_t* p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
                    r += (uint64_t)p[0] * p[3]; g += (uint64_t)p[1] * p[3]; b += (uint64_t)p[2] * p[3];
                    a += p[3]; n++;
                }
            if (!a) continue;
            uint8_t* o = &(*out)[((size_t)(oy + dy) * (size_t)s + (size_t)(ox + dx)) * 4];
            o[0] = (uint8_t)(r / a); o[1] = (uint8_t)(g / a); o[2] = (uint8_t)(b / a); o[3] = (uint8_t)(a / n);
        }
    }
    return true;
}

static const char* stbi_reason() {
    const char* r = stbi_failure_reason();
    return r ? r : "unknown";
}

bool rom_boxart_decode(const uint8_t* data, size_t len, const RomBoxartLimits& lim, unsigned side,
                       std::vector<uint8_t>* rgba, int* w, int* h, const char** why) {
    *w = *h = 0;
    if (!data || len == 0 || len > lim.max_file_bytes) { *why = "empty or over the file-size cap"; return false; }
    int comp = 0;
    if (!stbi_info_from_memory(data, (int)len, w, h, &comp)) { *why = stbi_reason(); return false; }
    if (!rom_boxart_dims_ok(*w, *h, lim)) { *why = "dimensions over the box-art limit"; return false; }
    unsigned char* px = stbi_load_from_memory(data, (int)len, w, h, &comp, 4);
    if (!px) { *why = stbi_reason(); return false; }
    bool ok = rom_boxart_fit_rgba(px, *w, *h, side, rgba);
    stbi_image_free(px);
    if (!ok) *why = "icon downscale failed";
    return ok;
}

bool rom_boxart_load(const std::string& abs, const RomBoxartLimits& lim, unsigned side,
                     std::vector<uint8_t>* rgba) {
    struct stat st;
    if (stat(abs.c_str(), &st) != 0) { printf("[roms] boxart %s missing\n", abs.c_str()); return false; }
    if (st.st_size <= 0 || (unsigned long long)st.st_size > (unsigned long long)lim.max_file_bytes) {
        printf("[roms] boxart %s skipped: %lld bytes (cap %zu)\n", abs.c_str(), (long long)st.st_size,
               lim.max_file_bytes);
        return false;
    }
    unsigned long long t0 = wall_now_ms();
    std::vector<uint8_t> bytes;
    if (!rom_read_whole(abs, &bytes)) { printf("[roms] boxart %s read failed\n", abs.c_str()); return false; }
    int w = 0, h = 0;
    const char* why = "";
    if (!rom_boxart_decode(bytes.data(), bytes.size(), lim, side, rgba, &w, &h, &why)) {
        printf("[roms] boxart %s decode failed (%dx%d): %s\n", abs.c_str(), w, h, why);
        return false;
    }
    printf("[roms] boxart %s ok %dx%d -> %ux%u in %llu ms\n", abs.c_str(), w, h, side, side,
           wall_ms_since(t0));
    return true;
}
