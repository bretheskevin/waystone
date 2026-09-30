#include "snapshot_browse.h"
#include "snapshot.h"   // snapshot_sanitize_key
#include "base64.h"     // base64_encode
#include "json.h"       // json_escape

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

// -- normalize_game_name --
// Port of core/src/model.rs:120 normalize_game_name. Keep in sync with Rust source.
// Keeps only ASCII [A-Za-z0-9], lowercased; drops everything else (incl. multi-byte UTF-8).
std::string normalize_game_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') {
            out += (char)(c + ('a' - 'A'));
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += c;
        }
    }
    return out;
}

std::string snapshot_key_dir(const char* system, const std::string& game_name) {
    std::string group_key = std::string(system) + "/" +
                            normalize_game_name(game_name) + "/main";
    return snapshot_sanitize_key(group_key);
}

std::string history_timestamp() {
    time_t now = time(0);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &t);
    return buf;
}

std::string human_timestamp(const std::string& ts) {
    std::string d = ts;
    if (!d.empty() && d.back() == 'Z') d.pop_back();
    size_t dot = d.find('.');
    if (dot != std::string::npos) d.resize(dot);
    if (d.size() < 15 || d[8] != 'T') return ts;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4s-%.2s-%.2s %.2s:%.2s",
             d.c_str(), d.c_str() + 4, d.c_str() + 6, d.c_str() + 9,
             d.c_str() + 11);
    return buf;
}

std::string human_size(unsigned long long bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024ULL) {
        snprintf(buf, sizeof(buf), "%.1f MB",
                 (double)bytes / (double)(1024ULL * 1024ULL));
    } else if (bytes >= 1024ULL) {
        snprintf(buf, sizeof(buf), "%.1f KB",
                 (double)bytes / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%llu B", bytes);
    }
    return buf;
}

// -- Recursive directory walk helpers --

struct WalkFile {
    std::string relative_path;  // relative to the walk root
    unsigned long long size;
};

static void walk_dir_recursive(const std::string& base,
                               const std::string& prefix,
                               std::vector<WalkFile>& out) {
    DIR* d = opendir(base.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != 0) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        std::string child_path = base + "/" + ent->d_name;
        std::string child_rel = prefix.empty()
                                    ? std::string(ent->d_name)
                                    : prefix + "/" + ent->d_name;
        struct stat st;
        if (stat(child_path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_dir_recursive(child_path, child_rel, out);
        } else if (S_ISREG(st.st_mode)) {
            WalkFile wf;
            wf.relative_path = child_rel;
            wf.size = (unsigned long long)st.st_size;
            out.push_back(wf);
        }
    }
    closedir(d);
}

// -- list_snapshots --

std::vector<SnapshotEntry> list_snapshots(const char* backups_root,
                                          const char* key_dir) {
    std::vector<SnapshotEntry> entries;
    std::string dir = std::string(backups_root) + "/" + key_dir;

    DIR* d = opendir(dir.c_str());
    if (!d) return entries;

    struct dirent* ent;
    while ((ent = readdir(d)) != 0) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        std::string ts_path = dir + "/" + ent->d_name;
        struct stat st;
        if (stat(ts_path.c_str(), &st) != 0) continue;
        if (!S_ISDIR(st.st_mode)) continue;

        std::vector<WalkFile> files;
        walk_dir_recursive(ts_path, "", files);

        unsigned long long total = 0;
        for (size_t i = 0; i < files.size(); i++) total += files[i].size;

        SnapshotEntry e;
        e.timestamp = ent->d_name;
        e.path = ts_path;
        e.file_count = files.size();
        e.total_bytes = total;
        entries.push_back(e);
    }
    closedir(d);

    // Sort newest-first (timestamps are lexically sortable: YYYYMMDDTHHMMSSZ).
    // std::sort with a lambda is C++11-compatible on both platforms.
    std::sort(entries.begin(), entries.end(),
              [](const SnapshotEntry& a, const SnapshotEntry& b) {
                  return a.timestamp > b.timestamp;
              });

    return entries;
}

// -- snapshot_to_flat_files_json --

// Strip the first two slash-delimited components from a path.
// Paths stored by write_snapshot are "<title_dir>/<slot>/<save-relative>".
// Removing <title_dir> and <slot> yields the save-relative path that
// write_save_files expects.
static std::string strip_two_components(const std::string& path) {
    size_t first = path.find('/');
    if (first == std::string::npos) return "";
    size_t second = path.find('/', first + 1);
    if (second == std::string::npos) return "";
    return path.substr(second + 1);
}

// Extract the 2nd slash-delimited component (the slot) from a path of the
// form "<title_dir>/<slot>/<rel>". Returns "" if the path has fewer than 2 components.
static std::string extract_slot(const std::string& path) {
    size_t first = path.find('/');
    if (first == std::string::npos) return "";
    size_t second = path.find('/', first + 1);
    if (second == std::string::npos) return path.substr(first + 1);
    return path.substr(first + 1, second - first - 1);
}

std::string snapshot_to_flat_files_json(const char* snapshot_dir, const char* slot_filter) {
    std::vector<WalkFile> files;
    walk_dir_recursive(std::string(snapshot_dir), "", files);

    if (files.empty()) return "[]";

    std::string result = "[";
    bool first = true;

    for (size_t i = 0; i < files.size(); i++) {
        std::string stripped = strip_two_components(files[i].relative_path);
        if (stripped.empty()) continue;
        if (slot_filter != nullptr &&
            extract_slot(files[i].relative_path) != slot_filter) continue;

        std::string full_path = std::string(snapshot_dir) + "/" +
                                files[i].relative_path;
        FILE* f = fopen(full_path.c_str(), "rb");
        if (!f) return "";

        std::vector<uint8_t> content;
        if (files[i].size > 0) {
            content.resize((size_t)files[i].size);
            size_t nread = fread(content.data(), 1, content.size(), f);
            fclose(f);
            if (nread != content.size()) return "";
        } else {
            fclose(f);
        }

        std::string b64 = content.empty()
                              ? ""
                              : base64_encode(content.data(), content.size());

        if (!first) result += ",";
        first = false;

        result += "{\"path\":\"";
        result += json_escape(stripped);
        result += "\",\"data_b64\":\"";
        result += b64;
        result += "\"}";
    }

    result += "]";
    return result;
}
