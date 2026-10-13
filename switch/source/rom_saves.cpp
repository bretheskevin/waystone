#include "rom_saves.h"
#include "rom_parse.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <climits>
#include <ctime>
#include <chrono>
#include <dirent.h>
#include <map>
#include <sys/stat.h>
#include <string>
#include <vector>

struct Vault;
extern "C" {
#include "waystone.h"
}

static const char* SD_PREFIX = "sdmc:/";
static const char* const ROMS_ROOTS[] = {"roms", "retroarch/roms"};
static const char RETROARCH_SAVES[] = "retroarch/saves";
static const char* ROM_ID_CACHE = "sdmc:/waystone/rom_ids";
static const int ROMS_MAX_DEPTH = 4;
static const size_t CRC_CHUNK = 65536;
static const size_t PSX_SAMPLE_LEN = 4u * 1024 * 1024; // first + last 4 MiB
static const unsigned long long CUE_MAX_BYTES = 64 * 1024;

typedef std::map<std::string, RomIdCacheEntry> RomIdCache;

static std::string sd_abs(const std::string& rel) { return std::string(SD_PREFIX) + rel; }

static unsigned long long now_ms() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static unsigned long long ms_since(unsigned long long t0) { return now_ms() - t0; }

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

// mkdir -p for sdmc:/ paths (POSIX mkdir, EEXIST tolerated). Logs and returns
// false on the first real failure.
static bool rom_mkdir_p(const std::string& dir) {
    if (dir.empty()) return false;
    std::string cur;
    size_t i = 0;
    if (dir.compare(0, 6, "sdmc:/") == 0) { cur = "sdmc:/"; i = 6; }
    while (i <= dir.size()) {
        size_t slash = dir.find('/', i);
        std::string comp = dir.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!comp.empty()) {
            if (!cur.empty() && cur[cur.size() - 1] != '/') cur += '/';
            cur += comp;
            if (mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST) {
                printf("[saves] rom write: mkdir %s failed (errno %d)\n", cur.c_str(), errno);
                return false;
            }
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("[saves] rom write: %s exists but is not a directory\n", dir.c_str());
        return false;
    }
    return true;
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
    if (!rom_mkdir_p("sdmc:/waystone")) printf("[roms] mkdir sdmc:/waystone failed\n");
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

struct PsxReadCtx { FILE* f; };

// devkitPro newlib has no fseeko: fseek with a 32-bit long offset suffices because PSX
// images are <= ~700 MiB. An offset beyond LONG_MAX can't belong to a PSX image — log + fail.
static uintptr_t psx_read_at(void* user, uint64_t off, uint8_t* buf, uintptr_t len) {
    PsxReadCtx* c = static_cast<PsxReadCtx*>(user);
    if (off > (uint64_t)LONG_MAX) {
        printf("[roms] psx read offset %llu over LONG_MAX\n", (unsigned long long)off);
        return 0;
    }
    if (fseek(c->f, (long)off, SEEK_SET) != 0) return 0;
    return fread(buf, 1, len, c->f);
}

static std::string lower_ext(const std::string& file_name) {
    size_t dot = file_name.rfind('.');
    if (dot == std::string::npos) return "";
    std::string e = file_name.substr(dot + 1);
    for (size_t i = 0; i < e.size(); i++) e[i] = (char)tolower((unsigned char)e[i]);
    return e;
}

// size + CRC32(first 4 MiB + last 4 MiB); streamed in CRC_CHUNK pieces.
static bool psx_sample_crc_and_size(const std::string& abs, uint64_t* out_size, uint32_t* out_crc) {
    FILE* f = fopen(abs.c_str(), "rb");
    if (!f) { printf("[roms] fopen %s for sampling failed\n", abs.c_str()); return false; }
    struct stat st;
    // stat by path: newlib has no fileno/fstat. PSX images are <= ~700 MiB, so the
    // 32-bit long offsets used by fseek below are sufficient.
    if (stat(abs.c_str(), &st) != 0) {
        printf("[roms] stat %s failed\n", abs.c_str());
        fclose(f);
        return false;
    }
    uint64_t size = (uint64_t)st.st_size;
    std::vector<uint8_t> buf(CRC_CHUNK);
    uint32_t crc = 0;
    uint64_t head = size < (uint64_t)PSX_SAMPLE_LEN ? size : (uint64_t)PSX_SAMPLE_LEN;
    uint64_t done = 0;
    bool ok = true;
    while (done < head) {
        size_t want = (size_t)((head - done) < (uint64_t)buf.size() ? (head - done) : (uint64_t)buf.size());
        size_t n = fread(buf.data(), 1, want, f);
        if (n == 0) { ok = !ferror(f) ? (done == head) : false; break; }
        crc = ws_crc32_update(crc, buf.data(), n);
        done += n;
    }
    if (ok && size > (uint64_t)PSX_SAMPLE_LEN) {
        uint64_t tail_off = size - (uint64_t)PSX_SAMPLE_LEN;
        if (tail_off > (uint64_t)LONG_MAX) {
            // A file over LONG_MAX can't be a PSX image; fseek's long offset would truncate.
            printf("[roms] %s too large to sample (%llu bytes over LONG_MAX)\n",
                   abs.c_str(), (unsigned long long)size);
            ok = false;
        } else if (fseek(f, (long)tail_off, SEEK_SET) != 0) ok = false;
        done = 0;
        while (ok && done < (uint64_t)PSX_SAMPLE_LEN) {
            size_t want = (size_t)(((uint64_t)PSX_SAMPLE_LEN - done) < (uint64_t)buf.size()
                                       ? ((uint64_t)PSX_SAMPLE_LEN - done)
                                       : (uint64_t)buf.size());
            size_t n = fread(buf.data(), 1, want, f);
            if (n == 0) { ok = !ferror(f); break; }
            crc = ws_crc32_update(crc, buf.data(), n);
            done += n;
        }
    }
    fclose(f);
    if (!ok) { printf("[roms] sampling %s failed mid-read\n", abs.c_str()); return false; }
    *out_size = size;
    *out_crc = crc;
    return true;
}

// Resolve a .cue to its first data-track FILE, joined to the cue's directory.
// Rejects absolute paths and ".." (path safety). false on unreadable/unsafe cue.
static bool resolve_cue_data_file(const std::string& cue_abs, std::string* out) {
    std::vector<uint8_t> cue;
    if (!read_file_prefix(cue_abs, (size_t)CUE_MAX_BYTES, &cue) || cue.empty()) {
        printf("[roms] cue %s unreadable\n", cue_abs.c_str());
        return false;
    }
    char* bin = ws_rom_psx_cue_data_file(cue.data(), cue.size());
    if (!bin) {
        printf("[roms] cue %s: %s\n", cue_abs.c_str(), ffi_err());
        return false;
    }
    std::string bin_name = bin;
    ws_string_free(bin);
    for (size_t i = 0; i < bin_name.size(); i++)
        if (bin_name[i] == '\\') bin_name[i] = '/';
    if (bin_name.empty() || bin_name[0] == '/' || bin_name.find("..") != std::string::npos) {
        printf("[roms] cue %s: unsafe data file '%s' rejected\n", cue_abs.c_str(), bin_name.c_str());
        return false;
    }
    *out = cue_abs.substr(0, cue_abs.rfind('/')) + "/" + bin_name;
    return true;
}

static bool compute_psx_identity(const std::string& abs, const std::string& rom_file_name,
                                 std::string* out_id) {
    std::string ext = lower_ext(rom_file_name);
    std::string target = abs;
    bool can_parse_iso = true;
    if (ext == "cue") {
        if (!resolve_cue_data_file(abs, &target)) return false;
    } else if (ext == "chd") {
        can_parse_iso = false; // no CHD decoder -> sampled-hash fallback
    }
    uint64_t size = 0;
    uint32_t crc = 0;
    unsigned long long t0 = now_ms();
    if (!psx_sample_crc_and_size(target, &size, &crc)) return false;
    printf("[roms] sampled %s: %llu bytes crc=%08lX in %llu ms\n", target.c_str(),
           (unsigned long long)size, (unsigned long)crc, ms_since(t0));
    PsxReadCtx ctx;
    ctx.f = 0;
    if (can_parse_iso) {
        ctx.f = fopen(target.c_str(), "rb");
        if (!ctx.f)
            printf("[roms] fopen %s for serial parse failed; using sampled fallback\n",
                   target.c_str());
    }
    char* id = ws_rom_psx_identity(ctx.f ? psx_read_at : 0, &ctx, size, crc);
    if (ctx.f) fclose(ctx.f);
    if (!id) {
        printf("[roms] ws_rom_psx_identity failed for %s: %s\n", abs.c_str(), ffi_err());
        return false;
    }
    *out_id = id;
    ws_string_free(id);
    return true;
}

static std::string base_name_of(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool compute_identity(const std::string& abs, const std::string& system,
                             const std::vector<uint8_t>& header, std::string* out_id) {
    if (system == "psx") return compute_psx_identity(abs, base_name_of(abs), out_id);
    const uint8_t* h = header.empty() ? 0 : header.data();
    int need = ws_rom_needs_full_hash(system.c_str(), h, header.size());
    if (need < 0) {
        printf("[roms] ws_rom_needs_full_hash(%s) failed: %s\n", system.c_str(), ffi_err());
        return false;
    }
    uint32_t crc = 0;
    if (need == 1) {
        unsigned long long t0 = now_ms();
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

std::vector<TitleInfo> scan_rom_titles() {
    unsigned long long t0 = now_ms();
    std::vector<TitleInfo> out;
    std::vector<std::string> rels;
    for (const char* root : ROMS_ROOTS) walk_rel(root, 0, ROMS_MAX_DEPTH, rels);
    walk_rel(RETROARCH_SAVES, 0, 0, rels); // depth 0; per-core save subdirs out of scope
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
        std::string rom_file_name = row.rom_path.substr(row.rom_path.rfind('/') + 1);
        // A .cue's identity derives from the referenced data file: key the cache
        // entry on the bin's size+mtime so a re-ripped bin invalidates the cache
        // even when the cue itself is untouched.
        std::string stat_abs = abs;
        if (row.system == "psx" && lower_ext(rom_file_name) == "cue" &&
            !resolve_cue_data_file(abs, &stat_abs)) { failed++; continue; }
        struct stat st;
        if (stat(stat_abs.c_str(), &st) != 0) { printf("[roms] stat %s failed (skipping)\n", stat_abs.c_str()); failed++; continue; }
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
        t.title_id = 0x8000000000000000ull | rom_synthetic_unique_id(row.system, e.rom_id);
        t.source = SourceRom;
        t.system = row.system;
        t.rom_id = e.rom_id;
        t.rom_path = abs;
        t.rom_file_name = rom_file_name;
        t.save_dir = sd_abs(row.save_dir);
        for (size_t s = 0; s < row.save_paths.size(); s++) t.save_paths.push_back(sd_abs(row.save_paths[s]));
        // t.icon_path stays empty: no box art on Switch v1.
        char* dn = ws_rom_display_name(row.system.c_str(), header.empty() ? 0 : header.data(),
                                       header.size(), rom_file_name.c_str());
        if (dn) { t.name = dn; ws_string_free(dn); }
        else { printf("[roms] ws_rom_display_name failed: %s\n", ffi_err()); t.name = rom_file_name; }
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

std::string rom_saves_mtime(const TitleInfo& title) {
    time_t newest = 0;
    for (size_t i = 0; i < title.save_paths.size(); i++) {
        struct stat st;
        if (stat(title.save_paths[i].c_str(), &st) == 0 && st.st_mtime > newest)
            newest = st.st_mtime;
    }
    if (newest == 0) return "";
    char buf[24];
    struct tm tmv;
    if (!gmtime_r(&newest, &tmv)) return "";
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

int write_rom_saves(const TitleInfo& title, const std::vector<FileTreeEntry>& entries) {
    if (title.save_dir.empty()) { printf("[saves] rom write: empty save_dir for %s\n", title.rom_file_name.c_str()); return -1; }
    if (!rom_mkdir_p(title.save_dir)) { printf("[saves] rom write: mkdir %s FAILED\n", title.save_dir.c_str()); return -1; }
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
