#include "rom_parse.h"
#include <cstdio>
#include <cstdlib>

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

bool parse_rom_pair_tsv(const char* data, size_t len, bool leading_flavor, const char* flavor,
                        std::vector<RomPairingRow>* out) {
    const size_t first = leading_flavor ? 1 : 0;
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
        if (f.size() < first + 3 || (leading_flavor && f[0].empty())) { out->clear(); return false; }
        RomPairingRow r;
        r.flavor = leading_flavor ? f[0] : std::string(flavor ? flavor : "");
        r.system = f[first]; r.rom_path = f[first + 1]; r.save_dir = f[first + 2];
        for (size_t i = first + 3; i < f.size(); i++) r.save_paths.push_back(f[i]);
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
    if (s == "n64") return "N64";
    if (s == "psx") return "PSX";
    return "ROM";
}

bool rom_needs_header(const std::string& system) {
    return system == "nds" || system == "gba" || system == "n64";
}

uint32_t rom_fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < s.size(); i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

uint32_t rom_synthetic_unique_id(const std::string& system, const std::string& rom_id) {
    return 0x80000000u | (rom_fnv1a(system + "/" + rom_id) & 0x7FFFFFFFu);
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

std::string rom_join(const std::vector<std::string>& items, char sep) {
    std::string joined;
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) joined += sep;
        joined += items[i];
    }
    return joined;
}

bool rom_hidden_name(const char* name) { return name && name[0] == '.'; }
