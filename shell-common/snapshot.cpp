#include "snapshot.h"
#include "base64.h"
#include "json.h"
#define JSMN_HEADER
#include "jsmn.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <vector>

std::string snapshot_sanitize_key(const std::string& key) {
    std::string out;
    out.reserve(key.size());
    for (size_t i = 0; i < key.size(); ) {
        if (key[i] == '/' || key[i] == '\\') {
            out += '_';
            i++;
        } else if (key[i] == '.' && i + 1 < key.size() && key[i + 1] == '.') {
            out += '_';
            i += 2;
        } else {
            out += key[i++];
        }
    }
    return out;
}

// Create each directory component along path, ignoring EEXIST.
// The first slash-delimited component is treated as a device prefix (e.g. "sdmc:") and skipped.
// Returns false on the first mkdir failure that is NOT EEXIST.
static bool mkdir_p(const std::string& path) {
    std::string current;
    bool first = true;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        if (slash == std::string::npos) slash = path.size();
        std::string component = path.substr(start, slash - start);
        if (first) {
            current = component; // device prefix, e.g. "sdmc:" — skip mkdir
            first = false;
        } else if (!component.empty()) {
            current += "/" + component;
            int r = mkdir(current.c_str(), 0777);
            if (r != 0 && errno != EEXIST) return false;
        }
        if (slash >= path.size()) break;
        start = slash + 1;
    }
    return true;
}

static const int SNAP_MAX_TOKENS = 2048;

bool write_snapshot(const char* backup_dir, const char* files_json) {
    // Parse outer RawTree object {"files":[...]} with jsmn.
    jsmn_parser parser;
    jsmntok_t tokens[SNAP_MAX_TOKENS];
    jsmn_init(&parser);
    int n = jsmn_parse(&parser, files_json, strlen(files_json), tokens, SNAP_MAX_TOKENS);
    if (n < 1 || tokens[0].type != JSMN_OBJECT) return false;

    // Find the "files" key and its array value.
    int arr_tok = -1;
    for (int i = 1; i + 1 < n; i++) {
        if (tokens[i].type != JSMN_STRING) continue;
        int len = tokens[i].end - tokens[i].start;
        if (len == 5 && strncmp(files_json + tokens[i].start, "files", 5) == 0) {
            int vi = i + 1;
            if (vi < n && tokens[vi].type == JSMN_ARRAY) {
                arr_tok = vi;
            }
            break;
        }
    }

    if (arr_tok == -1) return false;
    if (tokens[arr_tok].size == 0) return true; // empty = nothing to back up

    // Extract the array substring and split into per-file entry strings.
    std::string arr_str(files_json + tokens[arr_tok].start,
                        static_cast<size_t>(tokens[arr_tok].end - tokens[arr_tok].start));
    std::vector<std::string> entries = json_split_array(arr_str.c_str());
    if (entries.empty()) return true;

    // Normalise backup_dir (strip trailing slash for consistent path building).
    std::string dir(backup_dir);
    while (!dir.empty() && dir.back() == '/') dir.pop_back();

    if (!mkdir_p(dir)) return false;

    for (const auto& entry : entries) {
        std::string path     = json_get_string(entry.c_str(), "path");
        std::string data_b64 = json_get_string(entry.c_str(), "data_b64");

        if (path.empty()) return false;

        std::vector<uint8_t> bytes;
        if (!data_b64.empty()) {
            bytes = base64_decode(data_b64);
            if (bytes.empty()) return false;
        }

        // Create parent directories under backup_dir.
        size_t last_slash = path.rfind('/');
        if (last_slash != std::string::npos) {
            std::string parent = dir + "/" + path.substr(0, last_slash);
            if (!mkdir_p(parent)) return false;
        }

        std::string full_path = dir + "/" + path;
        FILE* f = fopen(full_path.c_str(), "wb");
        if (!f) return false;
        if (!bytes.empty()) {
            size_t written = fwrite(bytes.data(), 1, bytes.size(), f);
            fclose(f);
            if (written != bytes.size()) return false;
        } else {
            fclose(f);
        }
    }

    return true;
}
