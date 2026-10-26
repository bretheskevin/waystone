#include "rom_io.h"
#include "snapshot.h"  // fs_mkdir_p
#include "ffi_err.h"
#include "wall_clock.h"

#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

static const size_t CRC_CHUNK = 65536;
static const size_t PSX_SAMPLE_LEN = 4u * 1024 * 1024; // first + last 4 MiB
static const unsigned long long CUE_MAX_BYTES = 64 * 1024;

std::string rom_sd_abs(const std::string& rel) { return std::string(ROM_SD_PREFIX) + rel; }

static bool entry_is_dir(const std::string& abs, const struct dirent* ent) {
#ifdef DT_DIR
    if (ent->d_type == DT_DIR) return true;
    if (ent->d_type == DT_REG) return false;
#endif
    struct stat st;
    return stat(abs.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void rom_walk_rel(const std::string& rel_dir, int depth, int max_depth, std::vector<std::string>& out) {
    std::string abs = rom_sd_abs(rel_dir);
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
        if (entry_is_dir(rom_sd_abs(child), ent)) {
            if (depth < max_depth) rom_walk_rel(child, depth + 1, max_depth, out);
        } else {
            out.push_back(child);
        }
    }
    closedir(d);
}

static void load_id_cache(RomIdCache& cache) {
    FILE* f = fopen(ROM_ID_CACHE_PATH, "r");
    if (!f) { printf("[roms] no id cache at %s (first scan)\n", ROM_ID_CACHE_PATH); return; }
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

static void save_id_cache(const RomIdCache& cache) {
    if (!fs_mkdir_p("sdmc:/waystone")) printf("[roms] mkdir sdmc:/waystone failed\n");
    FILE* f = fopen(ROM_ID_CACHE_PATH, "w");
    if (!f) { printf("[roms] id cache write failed path=%s (non-fatal)\n", ROM_ID_CACHE_PATH); return; }
    for (RomIdCache::const_iterator it = cache.begin(); it != cache.end(); ++it) {
        std::string line = format_rom_id_cache_line(it->second) + "\n";
        fputs(line.c_str(), f);
    }
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    printf("[roms] id cache %s: %zu entr%s -> %s\n", ok ? "saved" : "write FAILED",
           cache.size(), cache.size() == 1 ? "y" : "ies", ROM_ID_CACHE_PATH);
}

bool rom_read_prefix(const std::string& abs, size_t n, std::vector<uint8_t>* out) {
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

bool rom_read_whole(const std::string& path, std::vector<uint8_t>* out) {
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

std::string rom_lower_ext(const std::string& file_name) {
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
bool rom_resolve_cue(const std::string& cue_abs, std::string* out) {
    std::vector<uint8_t> cue;
    if (!rom_read_prefix(cue_abs, (size_t)CUE_MAX_BYTES, &cue) || cue.empty()) {
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
    std::string ext = rom_lower_ext(rom_file_name);
    std::string target = abs;
    bool can_parse_iso = true;
    if (ext == "cue") {
        if (!rom_resolve_cue(abs, &target)) return false;
    } else if (ext == "chd") {
        can_parse_iso = false; // no CHD decoder -> sampled-hash fallback
    }
    uint64_t size = 0;
    uint32_t crc = 0;
    unsigned long long t0 = wall_now_ms();
    if (!psx_sample_crc_and_size(target, &size, &crc)) return false;
    printf("[roms] sampled %s: %llu bytes crc=%08lX in %llu ms\n", target.c_str(),
           (unsigned long long)size, (unsigned long)crc, wall_ms_since(t0));
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

std::string rom_base_name(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool compute_identity(const std::string& abs, const std::string& system,
                             const std::vector<uint8_t>& header, std::string* out_id) {
    if (system == "psx") return compute_psx_identity(abs, rom_base_name(abs), out_id);
    const uint8_t* h = header.empty() ? 0 : header.data();
    int need = ws_rom_needs_full_hash(system.c_str(), h, header.size());
    if (need < 0) {
        printf("[roms] ws_rom_needs_full_hash(%s) failed: %s\n", system.c_str(), ffi_err());
        return false;
    }
    uint32_t crc = 0;
    if (need == 1) {
        unsigned long long t0 = wall_now_ms();
        unsigned long long bytes = 0;
        printf("[roms] hashing %s (crc32)...\n", abs.c_str());
        if (!crc32_file(abs, &crc, &bytes)) return false;
        printf("[roms] hashed %s: %llu bytes crc=%08lX in %llu ms\n",
               abs.c_str(), bytes, (unsigned long)crc, wall_ms_since(t0));
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

// A .cue's identity derives from the referenced data file: key the cache entry on the bin's
// size+mtime so a re-ripped bin invalidates the cache even when the cue itself is untouched.
static bool identity_stat_path(const std::string& abs, const std::string& system, std::string* out) {
    *out = abs;
    if (system == "psx" && rom_lower_ext(rom_base_name(abs)) == "cue") return rom_resolve_cue(abs, out);
    return true;
}

void rom_id_scan_begin(RomIdScan& scan) { load_id_cache(scan.cache); }

bool rom_id_scan_row(RomIdScan& scan, const RomPairingRow& row, std::vector<uint8_t>* header,
                     std::string* rom_id) {
    std::string abs = rom_sd_abs(row.rom_path);
    std::string stat_abs;
    if (!identity_stat_path(abs, row.system, &stat_abs)) { scan.failed++; return false; }
    struct stat st;
    if (stat(stat_abs.c_str(), &st) != 0) {
        printf("[roms] stat %s failed (skipping)\n", stat_abs.c_str());
        scan.failed++;
        return false;
    }
    header->clear();
    if (rom_needs_header(row.system) && !rom_read_prefix(abs, ROM_HEADER_BYTES, header)) {
        scan.failed++;
        return false;
    }
    RomIdCacheEntry e;
    e.path = row.rom_path;
    e.size = (unsigned long long)st.st_size;
    e.mtime = (long long)st.st_mtime;
    e.system = row.system;
    RomIdCache::const_iterator hit = scan.cache.find(row.rom_path);
    if (hit != scan.cache.end() && hit->second.size == e.size && hit->second.mtime == e.mtime &&
        hit->second.system == e.system) {
        e.rom_id = hit->second.rom_id;
        scan.hits++;
    } else {
        if (!compute_identity(abs, row.system, *header, &e.rom_id)) { scan.failed++; return false; }
        scan.computed++;
    }
    scan.fresh[e.path] = e;
    *rom_id = e.rom_id;
    return true;
}

void rom_id_scan_end(const RomIdScan& scan) {
    if (scan.computed > 0 || scan.fresh.size() != scan.cache.size()) save_id_cache(scan.fresh);
    else printf("[roms] id cache unchanged (%zu entries)\n", scan.cache.size());
}

bool rom_pair_rows(const std::vector<std::string>& rels, const char* flavor,
                   std::vector<RomPairingRow>* rows) {
    std::string joined;
    for (size_t i = 0; i < rels.size(); i++) { joined += rels[i]; joined += '\n'; }
    WsBuf pairs = ws_rom_pair_flavored(reinterpret_cast<const uint8_t*>(joined.data()), joined.size(), flavor);
    if (!pairs.ptr) { printf("[roms] pairing (flavor=%s) failed: %s\n", flavor, ffi_err()); return false; }
    bool parsed = parse_rom_pair_tsv(reinterpret_cast<const char*>(pairs.ptr), pairs.len, rows);
    ws_buf_free(pairs);
    if (parsed) printf("[roms] flavor=%s: %zu ROM(s) paired\n", flavor, rows->size());
    else printf("[roms] malformed pairing output (flavor=%s)\n", flavor);
    return parsed;
}

bool rom_read_save_entries(const std::vector<std::string>& save_paths, const std::string& prefix,
                           bool strict, std::vector<FileTreeEntry>* out) {
    for (size_t i = 0; i < save_paths.size(); i++) {
        const std::string& p = save_paths[i];
        std::vector<uint8_t> bytes;
        if (!rom_read_whole(p, &bytes)) {
            printf("[saves] rom read %s FAILED%s\n", p.c_str(), strict ? "" : " (treated as absent)");
            if (strict) return false;
            continue;
        }
        printf("[saves] rom read %s (%zu bytes)\n", p.c_str(), bytes.size());
        out->push_back(FileTreeEntry(prefix + rom_base_name(p), bytes));
    }
    return true;
}

std::vector<uint8_t> rom_extract_saves(const std::string& system, const std::vector<std::string>& save_paths) {
    std::vector<FileTreeEntry> files;
    if (!rom_read_save_entries(save_paths, system + "/rom/", true, &files)) return std::vector<uint8_t>();
    std::vector<uint8_t> tree = file_tree_encode(files);
    printf("[saves] rom extract [%s]: %zu file(s), tree %zu bytes\n", system.c_str(), files.size(), tree.size());
    return tree;
}

int rom_write_saves(const std::string& save_dir, const std::vector<FileTreeEntry>& entries) {
    if (save_dir.empty()) { printf("[saves] rom write: empty save_dir\n"); return -1; }
    if (!fs_mkdir_p(save_dir)) { printf("[saves] rom write: mkdir %s FAILED\n", save_dir.c_str()); return -1; }
    int ret = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        const std::string& name = entries[i].first;
        const std::vector<uint8_t>& bytes = entries[i].second;
        if (!rom_save_name_safe(name)) { printf("[saves] rom write: unsafe name '%s' rejected\n", name.c_str()); ret = -1; continue; }
        std::string path = save_dir + "/" + name;
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
