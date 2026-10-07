#include "rom_parse.h"
#include "vendor/stb_image.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const size_t BANNER_ICON = 0x20;
static const size_t BANNER_PALETTE = 0x220;
static const size_t BANNER_TITLE_EN = 0x340;
static const size_t BANNER_TITLE_UNITS = 0x80;
static const char* BOXART_DIR = "_nds/TWiLightMenu/boxart/";

static void split_tabs(const std::string& line, std::vector<std::string>* out) {
    out->clear();
    size_t start = 0;
    while (true) {
        size_t tab = line.find('\t', start);
        out->push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
}

bool parse_rom_pair_tsv(const char* data, size_t len, std::vector<RomPairingRow>* out) {
    out->clear();
    std::string all(data ? data : "", data ? len : 0);
    size_t start = 0;
    std::vector<std::string> f;
    while (start < all.size()) {
        size_t nl = all.find('\n', start);
        if (nl == std::string::npos) nl = all.size();
        std::string line = all.substr(start, nl - start);
        start = nl + 1;
        if (line.empty()) continue;
        split_tabs(line, &f);
        if (f.size() < 3) { out->clear(); return false; }
        RomPairingRow r;
        r.system = f[0]; r.rom_path = f[1]; r.save_dir = f[2];
        for (size_t i = 3; i < f.size(); i++) r.save_paths.push_back(f[i]);
        out->push_back(r);
    }
    return true;
}

bool parse_rom_id_cache_line(const std::string& line, RomIdCacheEntry* out) {
    std::vector<std::string> f;
    split_tabs(line, &f);
    if (f.size() != 5 || f[0].empty() || f[3].empty() || f[4].empty()) return false;
    char* end = 0;
    unsigned long long size = strtoull(f[1].c_str(), &end, 10);
    if (f[1].empty() || *end != '\0') return false;
    long long mtime = strtoll(f[2].c_str(), &end, 10);
    if (f[2].empty() || *end != '\0') return false;
    out->path = f[0]; out->size = size; out->mtime = mtime; out->system = f[3]; out->rom_id = f[4];
    return true;
}

std::string format_rom_id_cache_line(const RomIdCacheEntry& e) {
    char nums[64];
    snprintf(nums, sizeof(nums), "\t%llu\t%lld\t", e.size, e.mtime);
    return e.path + nums + e.system + "\t" + e.rom_id;
}

uint32_t nds_banner_offset(const uint8_t* header, size_t len) {
    if (!header || len < 0x6C) return 0;
    return (uint32_t)header[0x68] | ((uint32_t)header[0x69] << 8) |
           ((uint32_t)header[0x6A] << 16) | ((uint32_t)header[0x6B] << 24);
}

static void append_utf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) { s += (char)cp; }
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

std::string utf16le_to_utf8(const uint8_t* p, size_t max_units) {
    std::string out;
    for (size_t i = 0; i < max_units; i++) {
        uint32_t u = p[2 * i] | (p[2 * i + 1] << 8);
        if (u == 0) break;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < max_units) {
            uint32_t lo = p[2 * (i + 1)] | (p[2 * (i + 1) + 1] << 8);
            if (lo >= 0xDC00 && lo <= 0xDFFF) { append_utf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00)); i++; continue; }
        }
        append_utf8(out, u);
    }
    return out;
}

std::string nds_banner_title(const uint8_t* banner, size_t len) {
    if (!banner || len < BANNER_TITLE_EN + BANNER_TITLE_UNITS * 2) return "";
    std::string raw = utf16le_to_utf8(banner + BANNER_TITLE_EN, BANNER_TITLE_UNITS);
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t nl = raw.find('\n', start);
        if (nl == std::string::npos) nl = raw.size();
        std::string l = raw.substr(start, nl - start);
        if (!l.empty()) lines.push_back(l);
        start = nl + 1;
    }
    if (lines.empty()) return "";
    size_t keep = lines.size() >= 2 ? lines.size() - 1 : 1;
    std::string out;
    for (size_t i = 0; i < keep; i++) { if (i) out += ' '; out += lines[i]; }
    return out;
}

bool nds_banner_icon_rgba5551(const uint8_t* banner, size_t len, std::vector<uint8_t>* out) {
    if (!banner || len < BANNER_PALETTE + 0x20) return false;
    uint16_t pal[16];
    for (int i = 0; i < 16; i++) {
        uint16_t c = banner[BANNER_PALETTE + 2 * i] | (banner[BANNER_PALETTE + 2 * i + 1] << 8);
        uint16_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
        pal[i] = (uint16_t)((r << 11) | (g << 6) | (b << 1) | (i == 0 ? 0 : 1));
    }
    out->assign(NDS_ICON_BYTES, 0);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            size_t tile = (size_t)((y / 8) * 4 + (x / 8));
            uint8_t byte = banner[BANNER_ICON + tile * 32 + (y % 8) * 4 + (x % 8) / 2];
            uint16_t px = pal[(x % 2 == 0) ? (byte & 0x0F) : (byte >> 4)];
            size_t o = (size_t)(y * 32 + x) * 2;
            (*out)[o] = (uint8_t)(px & 0xFF);
            (*out)[o + 1] = (uint8_t)(px >> 8);
        }
    }
    return true;
}

const char* rom_system_badge(const std::string& s) {
    if (s == "nds") return "DS";
    if (s == "gba") return "GBA";
    if (s == "gb") return "GB";
    if (s == "nes") return "NES";
    if (s == "snes") return "SNES";
    if (s == "md") return "MD";
    if (s == "sms") return "SMS";
    if (s == "gg") return "GG";
    if (s == "ngp") return "NGP";
    return "ROM";
}

bool rom_needs_header(const std::string& system) { return system == "nds" || system == "gba"; }

uint32_t rom_synthetic_unique_id(const std::string& system, const std::string& rom_id) {
    uint32_t h = 2166136261u;
    std::string key = system + "/" + rom_id;
    for (size_t i = 0; i < key.size(); i++) { h ^= (uint8_t)key[i]; h *= 16777619u; }
    return 0x80000000u | (h & 0x7FFFFFFFu);
}

std::string rom_group_key(const std::string& system, const std::string& rom_id, const char* slot) {
    std::string key = system + "/" + rom_id;
    if (slot) { key += "/"; key += slot; }
    return key;
}

bool rom_save_name_safe(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

bool title_name_less(const std::string& a, const std::string& b) {
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (unsigned char)ca < (unsigned char)cb;
    }
    return a.size() < b.size();
}

bool rom_hidden_name(const char* name) { return name && name[0] == '.'; }

std::vector<std::string> boxart_paths(const std::string& rom_file_name) {
    std::vector<std::string> out;
    out.push_back(std::string(BOXART_DIR) + rom_file_name + ".bmp");
    out.push_back(std::string(BOXART_DIR) + rom_file_name + ".png");
    return out;
}

bool boxart_dims_ok(int w, int h) { return w > 0 && h > 0 && w <= BOXART_MAX_W && h <= BOXART_MAX_H; }

bool boxart_icon_rgba5551(const uint8_t* rgba, int w, int h, std::vector<uint8_t>* out) {
    if (!rgba || w <= 0 || h <= 0) return false;
    const int side = (int)BOXART_ICON_SIDE;
    int dw = side, dh = side;
    if (w >= h) dh = (h * side + w / 2) / w;
    else dw = (w * side + h / 2) / h;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    const int ox = (side - dw) / 2, oy = (side - dh) / 2;
    out->assign(BOXART_ICON_BYTES, 0);
    for (int dy = 0; dy < dh; dy++) {
        int y0 = dy * h / dh, y1 = (dy + 1) * h / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int dx = 0; dx < dw; dx++) {
            int x0 = dx * w / dw, x1 = (dx + 1) * w / dw;
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) {
                    const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
                    r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3]; n++;
                }
            uint16_t px = 0;
            if (a) px = (uint16_t)((((r / a) >> 3) << 11) | (((g / a) >> 3) << 6) | (((b / a) >> 3) << 1) |
                                   (a / n >= 128 ? 1 : 0));
            size_t o = ((size_t)(oy + dy) * side + (size_t)(ox + dx)) * 2;
            (*out)[o] = (uint8_t)(px & 0xFF);
            (*out)[o + 1] = (uint8_t)(px >> 8);
        }
    }
    return true;
}

static const char* stbi_reason() {
    const char* r = stbi_failure_reason();
    return r ? r : "unknown";
}

bool boxart_decode_icon(const uint8_t* data, size_t len, std::vector<uint8_t>* icon,
                        int* w, int* h, const char** why) {
    *w = *h = 0;
    if (!data || len == 0 || len > BOXART_MAX_FILE_BYTES) { *why = "empty or over the file-size cap"; return false; }
    int comp = 0;
    if (!stbi_info_from_memory(data, (int)len, w, h, &comp)) { *why = stbi_reason(); return false; }
    if (!boxart_dims_ok(*w, *h)) { *why = "dimensions over TWiLight's box-art limit"; return false; }
    unsigned char* rgba = stbi_load_from_memory(data, (int)len, w, h, &comp, 4);
    if (!rgba) { *why = stbi_reason(); return false; }
    bool ok = boxart_icon_rgba5551(rgba, *w, *h, icon);
    stbi_image_free(rgba);
    if (!ok) *why = "icon conversion failed";
    return ok;
}

unsigned rom_icon_side(size_t icon_bytes) {
    if (icon_bytes == NDS_ICON_BYTES) return NDS_ICON_SIDE;
    if (icon_bytes == BOXART_ICON_BYTES) return BOXART_ICON_SIDE;
    return 0;
}
