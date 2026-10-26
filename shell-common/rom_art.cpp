#include "rom_art.h"
#include "ffi_err.h"

#include <climits>
#include <cstdio>

static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

bool rom_read_nds_banner(const std::string& abs, const std::vector<uint8_t>& header,
                         std::vector<uint8_t>* banner) {
    uint32_t off = ws_nds_banner_offset(header.empty() ? 0 : header.data(), header.size());
    if (!off) { printf("[roms] banner %s: none (offset 0)\n", abs.c_str()); return false; }
    if ((unsigned long)off > (unsigned long)LONG_MAX) {
        printf("[roms] banner %s: offset 0x%lX over LONG_MAX\n", abs.c_str(), (unsigned long)off);
        return false;
    }
    FILE* f = fopen(abs.c_str(), "rb");
    if (!f) { printf("[roms] banner %s: fopen failed\n", abs.c_str()); return false; }
    banner->assign(ROM_NDS_BANNER_BYTES, 0);
    size_t got = 0;
    if (fseek(f, (long)off, SEEK_SET) == 0) got = fread(&(*banner)[0], 1, banner->size(), f);
    fclose(f);
    banner->resize(got);
    printf("[roms] banner %s: off=0x%lX read=%zu\n", abs.c_str(), (unsigned long)off, got);
    return got > 0;
}

bool rom_decode_nds_banner(const std::vector<uint8_t>& banner, std::vector<uint8_t>* rgba,
                           std::string* title) {
    const uint8_t* p = banner.empty() ? 0 : banner.data();
    WsBuf icon = ws_nds_banner_icon_rgba(p, banner.size());
    if (!icon.ptr) {
        printf("[roms] banner decode failed (%zu bytes): %s\n", banner.size(), ffi_err());
        return false;
    }
    if (icon.len != ROM_NDS_ICON_RGBA_BYTES) {
        printf("[roms] banner decode: unexpected icon size %zu (want %zu)\n", (size_t)icon.len,
               ROM_NDS_ICON_RGBA_BYTES);
        ws_buf_free(icon);
        return false;
    }
    rgba->assign(icon.ptr, icon.ptr + icon.len);
    ws_buf_free(icon);
    char* t = ws_nds_banner_title(p, banner.size());
    if (t) { *title = t; ws_string_free(t); } else title->clear();
    return true;
}

std::vector<uint8_t> rom_encode_bmp_rgba(const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> out;
    if (!rgba || w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > (UINT32_MAX - 54u) / 4u) return out;
    const uint32_t pixels = (uint32_t)w * (uint32_t)h * 4u;
    const uint32_t file = 14u + 40u + pixels;
    out.resize(file, 0);
    uint8_t* p = &out[0];
    p[0] = 'B'; p[1] = 'M';
    put_le32(p + 2, file); put_le32(p + 10, 54);
    put_le32(p + 14, 40); put_le32(p + 18, (uint32_t)w); put_le32(p + 22, (uint32_t)h);
    p[26] = 1; p[28] = 32;           // planes, bpp; compression 0 = BI_RGB
    put_le32(p + 34, pixels);
    for (int y = 0; y < h; y++) {
        const uint8_t* src = rgba + (size_t)(h - 1 - y) * (size_t)w * 4;
        uint8_t* dst = p + 54 + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            dst[4 * x] = src[4 * x + 2]; dst[4 * x + 1] = src[4 * x + 1];
            dst[4 * x + 2] = src[4 * x]; dst[4 * x + 3] = src[4 * x + 3];
        }
    }
    return out;
}

std::string rom_external_rel(const std::string& path, const std::string& external_root) {
    const std::string kPrefix = ROM_EXTERNAL_SAVES;
    if (path == kPrefix) return external_root;
    if (path.compare(0, kPrefix.size() + 1, kPrefix + "/") == 0)
        return external_root + path.substr(kPrefix.size());
    return path;
}

std::vector<std::string> rom_twilight_boxart_paths(const std::string& rom_file_name) {
    std::vector<std::string> out;
    char* joined = ws_twilight_boxart_paths(rom_file_name.c_str());
    if (!joined) {
        printf("[roms] ws_twilight_boxart_paths(%s) failed: %s\n", rom_file_name.c_str(), ffi_err());
        return out;
    }
    std::string s = joined;
    ws_string_free(joined);
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) nl = s.size();
        if (nl > start) out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

std::string rom_retroarch_boxart_rel(const std::string& system, const std::string& rom_file_name) {
    char* p = ws_retroarch_boxart_path(system.c_str(), rom_file_name.c_str());
    if (!p) return "";
    std::string s = p;
    ws_string_free(p);
    return s;
}

std::string rom_display_name(const std::string& system, const std::vector<uint8_t>& header,
                             const std::string& rom_file_name) {
    char* dn = ws_rom_display_name(system.c_str(), header.empty() ? 0 : header.data(), header.size(),
                                   rom_file_name.c_str());
    if (!dn) {
        printf("[roms] ws_rom_display_name(%s) failed: %s\n", rom_file_name.c_str(), ffi_err());
        return rom_file_name;
    }
    std::string s = dn;
    ws_string_free(dn);
    return s;
}
