#include "rom_saves.h"
#include "rom_parse.h"
#include "snapshot.h"  // fs_mkdir_p

#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <sys/stat.h>

struct Vault;
extern "C" {
#include "waystone.h"
}

static const char* SD_PREFIX     = "sdmc:/";
static const char* ROMS_ROOT     = "roms";
static const char* GLOBAL_SAVES  = "_nds/TWiLightMenu/saves";
static const char* NGP_SAVES     = "data/ngpds";
static const char* ROM_ID_CACHE  = "sdmc:/waystone/rom_ids";
static const int   ROMS_MAX_DEPTH = 4;
static const size_t CRC_CHUNK    = 64 * 1024;

typedef std::map<std::string, RomIdCacheEntry> RomIdCache;

static std::string sd_abs(const std::string& rel) { return std::string(SD_PREFIX) + rel; }

static unsigned long long ms_since(u64 t0) { return (unsigned long long)(osGetTime() - t0); }

static const char* ffi_err() { return ws_last_error() ? ws_last_error() : "unknown"; }

static bool entry_is_dir(const std::string& abs, const struct dirent* ent) {
#ifdef DT_DIR
    if (ent->d_type == DT_DIR) return true;
    if (ent->d_type == DT_REG) return false;
#endif
    struct stat st;
    return stat(abs.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static void walk_rel(const std::string& rel_dir, int depth, int max_depth,
                     std::vector<std::string>& out) {
    std::string abs = sd_abs(rel_dir);
    DIR* d = opendir(abs.c_str());
    if (!d) {
        if (depth == 0) printf("[roms] opendir %s failed (absent? skipping)\n", abs.c_str());
        return;
    }
    struct dirent* ent;
    while ((ent = readdir(d)) != 0) {
        if (rom_hidden_name(ent->d_name)) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0)
                printf("[roms] skip hidden %s/%s\n", rel_dir.c_str(), ent->d_name);
            continue;
        }
        std::string child = rel_dir + "/" + ent->d_name;
        if (entry_is_dir(sd_abs(child), ent)) {
            if (depth < max_depth) walk_rel(child, depth + 1, max_depth, out);
        } else {
            out.push_back(child);
        }
    }
    closedir(d);
}

static void load_rom_id_cache(RomIdCache& cache) {
    FILE* f = fopen(ROM_ID_CACHE, "r");
    if (!f) { printf("[roms] no id cache at %s (first scan)\n", ROM_ID_CACHE); return; }
    char line[1024];
    int bad = 0;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r')) s.erase(s.size() - 1);
        if (s.empty()) continue;
        RomIdCacheEntry e;
        if (parse_rom_id_cache_line(s, &e)) cache[e.path] = e; else bad++;
    }
    fclose(f);
    printf("[roms] id cache loaded: %zu entr%s (%d malformed skipped)\n",
           cache.size(), cache.size() == 1 ? "y" : "ies", bad);
}

static void save_rom_id_cache(const RomIdCache& cache) {
    if (!fs_mkdir_p("sdmc:/waystone")) printf("[roms] mkdir sdmc:/waystone failed\n");
    FILE* f = fopen(ROM_ID_CACHE, "w");
    if (!f) { printf("[roms] id cache write failed path=%s (non-fatal)\n", ROM_ID_CACHE); return; }
    for (RomIdCache::const_iterator it = cache.begin(); it != cache.end(); ++it) {
        std::string line = format_rom_id_cache_line(it->second) + "\n";
        fputs(line.c_str(), f);
    }
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    printf("[roms] id cache %s: %zu entr%s -> %s\n", ok ? "saved" : "write FAILED",
           cache.size(), cache.size() == 1 ? "y" : "ies", ROM_ID_CACHE);
}

static bool read_file_prefix(const std::string& abs, size_t n, std::vector<uint8_t>* out) {
    FILE* f = fopen(abs.c_str(), "rb");
    if (!f) { printf("[roms] fopen %s failed\n", abs.c_str()); return false; }
    out->resize(n);
    size_t got = fread(out->data(), 1, n, f);
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) { printf("[roms] read header %s failed after %zu bytes\n", abs.c_str(), got); return false; }
    out->resize(got);
    return true;
}

static bool crc32_file(const std::string& abs, uint32_t* out_crc, unsigned long long* out_bytes) {
    FILE* f = fopen(abs.c_str(), "rb");
    if (!f) { printf("[roms] fopen %s for hashing failed\n", abs.c_str()); return false; }
    std::vector<uint8_t> buf(CRC_CHUNK);
    uint32_t crc = 0;
    unsigned long long total = 0;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        crc = ws_crc32_update(crc, buf.data(), n);
        total += n;
    }
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) printf("[roms] read error hashing %s after %llu bytes\n", abs.c_str(), total);
    *out_crc = crc;
    *out_bytes = total;
    return ok;
}

static bool compute_identity(const std::string& abs, const std::string& system,
                             const std::vector<uint8_t>& header, std::string* out_id) {
    const uint8_t* h = header.empty() ? 0 : header.data();
    int need = ws_rom_needs_full_hash(system.c_str(), h, header.size());
    if (need < 0) {
        printf("[roms] ws_rom_needs_full_hash(%s) failed: %s\n", system.c_str(), ffi_err());
        return false;
    }
    uint32_t crc = 0;
    if (need == 1) {
        u64 t0 = osGetTime();
        unsigned long long bytes = 0;
        printf("[roms] hashing %s (crc32)...\n", abs.c_str());
        if (!crc32_file(abs, &crc, &bytes)) return false;
        printf("[roms] hashed %s: %llu bytes crc=%08lX in %llu ms\n",
               abs.c_str(), bytes, (unsigned long)crc, ms_since(t0));
    }
    char* id = ws_rom_identity(system.c_str(), h, header.size(), crc, need == 1);
    if (!id) {
        printf("[roms] ws_rom_identity failed for %s: %s\n", abs.c_str(), ffi_err());
        return false;
    }
    *out_id = id;
    ws_string_free(id);
    return true;
}

static bool read_whole_file(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) out->insert(out->end(), chunk, chunk + n);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

static void read_nds_banner(const std::string& abs, const std::vector<uint8_t>& header,
                            std::string* name, std::vector<uint8_t>* icon) {
    uint32_t off = nds_banner_offset(header.data(), header.size());
    if (!off) { printf("[roms] banner %s: none (offset 0)\n", abs.c_str()); return; }
    FILE* f = fopen(abs.c_str(), "rb");
    if (!f) { printf("[roms] banner %s: fopen failed\n", abs.c_str()); return; }
    std::vector<uint8_t> banner(NDS_BANNER_BYTES);
    size_t got = 0;
    if (fseek(f, (long)off, SEEK_SET) == 0) got = fread(banner.data(), 1, banner.size(), f);
    fclose(f);
    banner.resize(got);
    *name = nds_banner_title(banner.data(), banner.size());
    if (!nds_banner_icon_rgba5551(banner.data(), banner.size(), icon)) icon->clear();
    printf("[roms] banner %s: off=0x%lX read=%zu title='%s' icon=%s\n", abs.c_str(),
           (unsigned long)off, got, name->c_str(), icon->empty() ? "none" : "ok");
}

static void read_boxart_icon(const std::string& rom_file_name, std::vector<uint8_t>* icon) {
    std::vector<std::string> rels = boxart_paths(rom_file_name);
    for (size_t i = 0; i < rels.size(); i++) {
        std::string abs = sd_abs(rels[i]);
        struct stat st;
        if (stat(abs.c_str(), &st) != 0) continue;
        u64 t0 = osGetTime();
        if ((unsigned long long)st.st_size > BOXART_MAX_FILE_BYTES) {
            printf("[roms] boxart %s skipped: %lld bytes over the %zu cap\n", abs.c_str(),
                   (long long)st.st_size, BOXART_MAX_FILE_BYTES);
            return;
        }
        std::vector<uint8_t> bytes;
        if (!read_whole_file(abs, &bytes)) { printf("[roms] boxart %s read failed\n", abs.c_str()); return; }
        int w = 0, h = 0;
        const char* why = "";
        if (!boxart_decode_icon(bytes.data(), bytes.size(), icon, &w, &h, &why)) {
            icon->clear();
            printf("[roms] boxart %s decode failed (%dx%d): %s\n", abs.c_str(), w, h, why);
            return;
        }
        printf("[roms] boxart %s ok %dx%d in %llu ms\n", abs.c_str(), w, h, ms_since(t0));
        return;
    }
    printf("[roms] boxart %s missing (.bmp too)\n", sd_abs(rels.back()).c_str());
}

std::vector<TitleInfo> scan_rom_titles() {
    u64 t0 = osGetTime();
    std::vector<TitleInfo> out;
    std::vector<std::string> rels;
    walk_rel(ROMS_ROOT, 0, ROMS_MAX_DEPTH, rels);
    walk_rel(GLOBAL_SAVES, 0, 0, rels);
    walk_rel(NGP_SAVES, 0, 0, rels);
    printf("[roms] walked %zu file(s) in %llu ms\n", rels.size(), ms_since(t0));
    if (rels.empty()) return out;

    std::string joined;
    for (size_t i = 0; i < rels.size(); i++) { joined += rels[i]; joined += '\n'; }
    WsBuf pairs = ws_rom_pair(reinterpret_cast<const uint8_t*>(joined.data()), joined.size());
    if (!pairs.ptr) { printf("[roms] ws_rom_pair failed: %s\n", ffi_err()); return out; }
    std::vector<RomPairingRow> rows;
    bool parsed = parse_rom_pair_tsv(reinterpret_cast<const char*>(pairs.ptr), pairs.len, &rows);
    ws_buf_free(pairs);
    if (!parsed) { printf("[roms] malformed ws_rom_pair output\n"); return out; }
    printf("[roms] %zu ROM(s) paired\n", rows.size());

    RomIdCache cache;
    load_rom_id_cache(cache);
    RomIdCache fresh;
    int hits = 0, computed = 0, failed = 0;
    for (size_t i = 0; i < rows.size(); i++) {
        const RomPairingRow& row = rows[i];
        std::string abs = sd_abs(row.rom_path);
        struct stat st;
        if (stat(abs.c_str(), &st) != 0) { printf("[roms] stat %s failed (skipping)\n", abs.c_str()); failed++; continue; }
        std::string rom_file_name = row.rom_path.substr(row.rom_path.rfind('/') + 1);
        std::vector<uint8_t> header;
        if (rom_needs_header(row.system) && !read_file_prefix(abs, ROM_HEADER_BYTES, &header)) { failed++; continue; }

        RomIdCacheEntry e;
        e.path = row.rom_path;
        e.size = (unsigned long long)st.st_size;
        e.mtime = (long long)st.st_mtime;
        e.system = row.system;
        RomIdCache::const_iterator hit = cache.find(row.rom_path);
        if (hit != cache.end() && hit->second.size == e.size && hit->second.mtime == e.mtime &&
            hit->second.system == e.system) {
            e.rom_id = hit->second.rom_id;
            hits++;
        } else {
            if (!compute_identity(abs, row.system, header, &e.rom_id)) { failed++; continue; }
            computed++;
        }
        fresh[e.path] = e;

        TitleInfo t;
        t.title_id = 0;
        t.unique_id = rom_synthetic_unique_id(row.system, e.rom_id);
        t.is_twl = false;
        t.source = SourceRom;
        t.system = row.system;
        t.rom_id = e.rom_id;
        t.rom_path = abs;
        t.rom_file_name = rom_file_name;
        t.save_dir = sd_abs(row.save_dir);
        for (size_t s = 0; s < row.save_paths.size(); s++) t.save_paths.push_back(sd_abs(row.save_paths[s]));
        if (row.system == "nds") read_nds_banner(abs, header, &t.name, &t.icon);
        else read_boxart_icon(rom_file_name, &t.icon);
        if (t.name.empty()) {
            char* dn = ws_rom_display_name(row.system.c_str(), header.empty() ? 0 : header.data(),
                                           header.size(), rom_file_name.c_str());
            if (dn) { t.name = dn; ws_string_free(dn); }
            else { printf("[roms] ws_rom_display_name failed: %s\n", ffi_err()); t.name = rom_file_name; }
        }
        printf("[roms] %s -> [%s] '%s' id=%s saves=%zu save_dir=%s\n", row.rom_path.c_str(),
               row.system.c_str(), t.name.c_str(), t.rom_id.c_str(), t.save_paths.size(),
               t.save_dir.c_str());
        out.push_back(t);
    }
    if (computed > 0 || fresh.size() != cache.size()) save_rom_id_cache(fresh);
    printf("[roms] scan done: %zu ROM title(s), %d cache hit(s), %d hashed, %d failed in %llu ms\n",
           out.size(), hits, computed, failed, ms_since(t0));
    return out;
}

std::vector<uint8_t> extract_rom_saves(const TitleInfo& title) {
    std::vector<FileTreeEntry> files;
    for (size_t i = 0; i < title.save_paths.size(); i++) {
        const std::string& p = title.save_paths[i];
        std::vector<uint8_t> bytes;
        if (!read_whole_file(p, &bytes)) {
            printf("[saves] rom extract: read %s FAILED\n", p.c_str());
            return std::vector<uint8_t>();
        }
        printf("[saves] rom extract: %s (%zu bytes)\n", p.c_str(), bytes.size());
        files.push_back(FileTreeEntry(title.system + "/rom/" + p.substr(p.rfind('/') + 1), bytes));
    }
    std::vector<uint8_t> tree = file_tree_encode(files);
    printf("[saves] rom extract %s: %zu file(s), tree %zu bytes\n",
           title.rom_file_name.c_str(), files.size(), tree.size());
    return tree;
}

int write_rom_saves(const TitleInfo& title, const std::vector<FileTreeEntry>& entries) {
    if (title.save_dir.empty()) { printf("[saves] rom write: empty save_dir for %s\n", title.rom_file_name.c_str()); return -1; }
    if (!fs_mkdir_p(title.save_dir)) { printf("[saves] rom write: mkdir %s FAILED\n", title.save_dir.c_str()); return -1; }
    int ret = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        const std::string& name = entries[i].first;
        const std::vector<uint8_t>& bytes = entries[i].second;
        if (!rom_save_name_safe(name)) { printf("[saves] rom write: unsafe name '%s' rejected\n", name.c_str()); ret = -1; continue; }
        std::string path = title.save_dir + "/" + name;
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { printf("[saves] rom write: fopen %s FAILED\n", path.c_str()); ret = -1; continue; }
        size_t w = bytes.empty() ? 0 : fwrite(bytes.data(), 1, bytes.size(), f);
        bool ok = (w == bytes.size());
        if (fclose(f) != 0) ok = false;
        printf("[saves] rom write: %s (%zu bytes) %s\n", path.c_str(), bytes.size(), ok ? "ok" : "FAILED");
        if (!ok) ret = -1;
    }
    return ret;
}
