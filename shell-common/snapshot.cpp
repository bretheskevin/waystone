#include "snapshot.h"
#include "file_tree.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
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

bool write_snapshot(const char* backup_dir, const uint8_t* ft_ptr, size_t ft_len) {
    std::vector<FileTreeEntry> files;
    if (!file_tree_decode(ft_ptr, ft_len, &files)) {
        printf("[saves] write_snapshot: file_tree_decode failed (len=%zu)\n", ft_len);
        return false;
    }
    if (files.empty()) {
        printf("[saves] write_snapshot: empty tree, nothing to back up\n");
        return true;
    }

    std::string dir(backup_dir);
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
    printf("[saves] write_snapshot: %zu file(s) -> %s\n", files.size(), dir.c_str());
    if (!mkdir_p(dir)) {
        printf("[saves] write_snapshot: mkdir failed %s\n", dir.c_str());
        return false;
    }

    for (size_t i = 0; i < files.size(); i++) {
        const std::string& path = files[i].first;
        const std::vector<uint8_t>& bytes = files[i].second;
        if (path.empty()) {
            printf("[saves] write_snapshot: empty path in entry %zu\n", i);
            return false;
        }

        size_t last_slash = path.rfind('/');
        if (last_slash != std::string::npos) {
            std::string parent = dir + "/" + path.substr(0, last_slash);
            if (!mkdir_p(parent)) {
                printf("[saves] write_snapshot: mkdir failed %s\n", parent.c_str());
                return false;
            }
        }

        std::string full_path = dir + "/" + path;
        FILE* f = fopen(full_path.c_str(), "wb");
        if (!f) {
            printf("[saves] write_snapshot: fopen failed %s\n", full_path.c_str());
            return false;
        }
        if (!bytes.empty()) {
            size_t written = fwrite(bytes.data(), 1, bytes.size(), f);
            fclose(f);
            if (written != bytes.size()) {
                printf("[saves] write_snapshot: short write %s\n", full_path.c_str());
                return false;
            }
        } else {
            fclose(f);
        }
    }

    printf("[saves] write_snapshot: done\n");
    return true;
}

// Recursively delete `path` (a file or directory) over the host FS.
// Directories: two-pass — collect the listing under the open cursor, close,
// then delete — so we never mutate under an opendir cursor.
// Per-delete failures are logged and reported via the return value (false),
// but deletion continues.
static bool remove_path_recursive(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        printf("[snapshot] prune: stat failed %s: %s (non-fatal)\n",
               path.c_str(), strerror(errno));
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (unlink(path.c_str()) != 0) {
            printf("[snapshot] prune: unlink failed %s: %s (non-fatal)\n",
                   path.c_str(), strerror(errno));
            return false;
        }
        return true;
    }

    DIR* d = opendir(path.c_str());
    if (!d) {
        printf("[snapshot] prune: opendir failed %s: %s (non-fatal)\n",
               path.c_str(), strerror(errno));
        return false;
    }
    std::vector<std::string> children;
    struct dirent* ent;
    while ((ent = readdir(d)) != 0) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        children.push_back(ent->d_name);
    }
    closedir(d);

    bool ok = true;
    for (size_t i = 0; i < children.size(); i++) {
        if (!remove_path_recursive(path + "/" + children[i])) ok = false;
    }
    if (rmdir(path.c_str()) != 0) {
        printf("[snapshot] prune: rmdir failed %s: %s (non-fatal)\n",
               path.c_str(), strerror(errno));
        return false;
    }
    return ok;
}

// Strict "YYYYMMDDTHHMMSSZ" check: exactly 16 chars — 8 digits, 'T',
// 6 digits, 'Z' (history_timestamp() format).
static bool is_ts_dir_name(const char* name) {
    if (strlen(name) != 16) return false;
    for (int i = 0; i < 8; i++)
        if (name[i] < '0' || name[i] > '9') return false;
    if (name[8] != 'T') return false;
    for (int i = 9; i < 15; i++)
        if (name[i] < '0' || name[i] > '9') return false;
    return name[15] == 'Z';
}

bool snapshot_prune(const char* backup_root, int keep) {
    if (keep < 1) keep = 1;

    std::string root(backup_root);
    while (!root.empty() && root.back() == '/') root.pop_back();

    DIR* d = opendir(root.c_str());
    if (!d) {
        printf("[snapshot] prune: cannot open %s, nothing to prune\n",
               root.c_str());
        return true; // nothing to prune is not an error
    }

    // Direct children of the game root are the ts dirs
    // (same layout assumption as list_snapshots). Only strict
    // "YYYYMMDDTHHMMSSZ" names participate: a partial write or a
    // foreign file/dir must be neither counted toward keep nor deleted.
    std::vector<std::string> dirs;
    size_t skipped = 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != 0) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        std::string child = root + "/" + ent->d_name;
        struct stat st;
        if (stat(child.c_str(), &st) != 0) continue;
        if (!S_ISDIR(st.st_mode) || !is_ts_dir_name(ent->d_name)) {
            skipped++;
            continue;
        }
        dirs.push_back(ent->d_name);
    }
    closedir(d);

    if (skipped > 0) {
        printf("[snapshot] prune skipped %zu non-snapshot entr(ies) under %s\n",
               skipped, root.c_str());
    }

    // Lexicographic order == chronological (YYYYMMDDTHHMMSSZ).
    std::sort(dirs.begin(), dirs.end());

    const int total = (int)dirs.size();
    bool ok = true;
    int removed = 0;
    for (size_t i = 0; i + (size_t)keep < dirs.size(); i++) {
        if (remove_path_recursive(root + "/" + dirs[i])) removed++;
        else ok = false;
    }

    if (removed > 0) {
        printf("[snapshot] pruned %s: kept %d of %d\n",
               root.c_str(), total - removed, total);
    }
    return ok;
}
