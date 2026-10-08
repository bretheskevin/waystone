#include "saves.h"
#include "file_tree.h"
#include "sync_rules.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <switch.h>

// Max icon JPEG size from NsApplicationControlData.
static const size_t ICON_MAX_BYTES = 0x20000;

// Cache icon JPEG to sdmc:/waystone/icons/<TID>.jpg (write once, skip if present).
// Returns the cached file path, or empty string on failure / empty icon.
static std::string cache_icon(uint64_t title_id, const uint8_t* icon, size_t icon_size) {
    if (icon_size < 3) return "";
    // Basic JPEG magic check (SOI: FF D8)
    if (icon[0] != 0xFF || icon[1] != 0xD8) return "";

    const char* dir = "sdmc:/waystone/icons";
    char path[128];
    snprintf(path, sizeof(path), "%s/%016lX.jpg", dir, title_id);

    // Skip write if already cached
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 0) return path;

    // Ensure directory exists
    mkdir("sdmc:/waystone", 0755);
    mkdir(dir, 0755);

    FILE* f = fopen(path, "wb");
    if (!f) return "";
    size_t written = fwrite(icon, 1, icon_size, f);
    fclose(f);
    if (written != icon_size) return "";

    printf("[titles] cached icon tid=%016lX -> %s\n", title_id, path);
    return path;
}

std::vector<TitleInfo> list_titles() {
    std::vector<TitleInfo> titles;

    if (R_FAILED(nsInitialize())) {
        printf("nsInitialize failed\n");
        return titles;
    }

    NsApplicationRecord records[256];
    s32 offset = 0;
    int icons_found = 0;

    while (true) {
        s32 got = 0;
        Result rc = nsListApplicationRecord(records, 256, static_cast<s64>(offset), &got);
        if (R_FAILED(rc) || got == 0) break;

        for (s32 i = 0; i < got; i++) {
            TitleInfo info;
            info.title_id = records[i].application_id;

            // Get display name AND icon from NACP control data. ~128 KiB -- heap-allocated.
            NsApplicationControlData* ctrl =
                static_cast<NsApplicationControlData*>(malloc(sizeof(NsApplicationControlData)));
            if (ctrl) {
                u64 actual = 0;
                Result nrc = nsGetApplicationControlData(
                    NsApplicationControlSource_Storage,
                    info.title_id, ctrl, sizeof(NsApplicationControlData), &actual);
                if (R_SUCCEEDED(nrc)) {
                    NacpLanguageEntry* lang = nullptr;
                    nacpGetLanguageEntry(&ctrl->nacp, &lang);
                    if (lang && lang->name[0] != '\0') {
                        info.name = lang->name;
                    }
                    // Icon JPEG follows the NACP in NsApplicationControlData.
                    // actual covers nacp + icon bytes; clamp to declared max.
                    size_t icon_size = 0;
                    if (actual > sizeof(NacpStruct)) {
                        icon_size = actual - sizeof(NacpStruct);
                        if (icon_size > ICON_MAX_BYTES) icon_size = ICON_MAX_BYTES;
                    }
                    info.icon_path = cache_icon(info.title_id, ctrl->icon, icon_size);
                    if (!info.icon_path.empty()) ++icons_found;
                }
                free(ctrl);
            }

            // Fallback: uppercase 16-char hex TID
            if (info.name.empty()) {
                char hex[17];
                snprintf(hex, sizeof(hex), "%016lX", info.title_id);
                info.name = hex;
            }

            titles.push_back(info);
        }

        offset += got;
    }

    nsExit();
    printf("[titles] extracted %zu titles (%d with icons)\n", titles.size(), icons_found);
    return titles;
}

// Recursively walk a directory and collect all files as (relative_path, content) pairs.
static void walk_dir(const char* base, const char* rel,
                     std::vector<std::pair<std::string, std::vector<uint8_t>>>* out,
                     long long* max_mtime) {
    std::string full = std::string(base);
    if (rel[0] != '\0') {
        full += "/";
        full += rel;
    }

    DIR* d = opendir(full.c_str());
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        std::string child_rel;
        if (rel[0] != '\0') {
            child_rel = std::string(rel) + "/" + ent->d_name;
        } else {
            child_rel = ent->d_name;
        }

        std::string child_full = std::string(base) + "/" + child_rel;

        struct stat st;
        if (stat(child_full.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            walk_dir(base, child_rel.c_str(), out, max_mtime);
        } else if (S_ISREG(st.st_mode)) {
            FILE* f = fopen(child_full.c_str(), "rb");
            if (!f) continue;
            std::vector<uint8_t> content(static_cast<size_t>(st.st_size));
            if (st.st_size > 0) {
                size_t got = fread(content.data(), 1, content.size(), f);
                if (got != content.size()) {
                    fclose(f);
                    continue; // short read — skip this file
                }
            }
            fclose(f);
            if ((long long)st.st_mtime > *max_mtime) *max_mtime = (long long)st.st_mtime;
            out->push_back({child_rel, content});
        }
    }
    closedir(d);
}

std::vector<uint8_t> extract_save_json(const TitleInfo& title, AccountUid uid, std::string* local_mtime) {
    // Mount save data
    Result rc = fsdevMountSaveData("save", title.title_id, uid);
    if (R_FAILED(rc)) {
        // FsError_TargetNotFound = 0x7D402 means no save exists for this title+user
        printf("[saves] fsdevMountSaveData failed: 0x%X\n", rc);
        return std::vector<uint8_t>();
    }

    // Walk the mounted filesystem
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    long long max_mtime = 0;
    walk_dir("save:", "", &files, &max_mtime);

    fsdevUnmountDevice("save");

    if (local_mtime) {
        *local_mtime = plausible_mtime_iso(max_mtime, (long long)time(nullptr));
        printf("[saves] %s: newest file mtime=%lld -> '%s'\n", title.name.c_str(), max_mtime,
               local_mtime->empty() ? "(unusable -> NewestWins treated as Prompt)" : local_mtime->c_str());
    }

    if (files.empty()) return std::vector<uint8_t>();

    // Wrap file paths in JKSV convention: <GameName>/main/<relative_path>
    // This lets ws_jksv_normalize parse title_dir=<GameName>, slot="main".
    std::vector<std::pair<std::string, std::vector<uint8_t>>> wrapped;
    wrapped.reserve(files.size());
    for (auto& f : files) {
        std::string jksv_path = title.name + "/main/" + f.first;
        wrapped.push_back({jksv_path, std::move(f.second)});
    }

    std::vector<uint8_t> tree = file_tree_encode(wrapped);
    printf("[saves] extracted %zu files -> file tree %zu bytes\n", wrapped.size(), tree.size());
    return tree;
}

bool get_active_account(AccountUid* out_uid) {
    if (R_FAILED(accountInitialize(AccountServiceType_Administrator))) {
        printf("accountInitialize failed\n");
        return false;
    }

    Result rc = accountGetPreselectedUser(out_uid);
    if (R_FAILED(rc) || !accountUidIsValid(out_uid)) {
        rc = accountGetLastOpenedUser(out_uid);
    }
    accountExit();

    if (R_FAILED(rc) || !accountUidIsValid(out_uid)) {
        printf("No valid user account found\n");
        return false;
    }
    return true;
}

std::string current_utc_time() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return buf;
}

std::string get_device_id() {
    const char* path = "sdmc:/waystone/device_id.txt";
    const char* dir = "sdmc:/waystone";

    // Try to read existing ID
    FILE* f = fopen(path, "r");
    if (f) {
        char buf[65] = {};
        size_t n = fread(buf, 1, 64, f);
        fclose(f);
        if (n >= 16) return std::string(buf, n);
    }

    // Generate new random ID (32 hex chars = 16 random bytes)
    uint8_t rand_bytes[16];
    randomGet(rand_bytes, sizeof(rand_bytes));
    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", rand_bytes[i]);
    }

    // Persist to SD card
    mkdir(dir, 0755);
    f = fopen(path, "w");
    if (f) {
        fwrite(hex, 1, 32, f);
        fclose(f);
    }

    return std::string(hex, 32);
}

int write_save_files(u64 title_id, AccountUid uid,
                     const uint8_t* ft_ptr, size_t ft_len) {
    std::vector<FileTreeEntry> entries;
    if (!file_tree_decode(ft_ptr, ft_len, &entries)) {
        printf("[saves] write_save_files: file_tree_decode failed (%zu bytes)\n", ft_len);
        return -1;
    }

    Result rc = fsdevMountSaveData("save", title_id, uid);
    if (R_FAILED(rc)) {
        printf("[saves] write_save_files: fsdevMountSaveData failed: 0x%X\n", rc);
        return -1;
    }
    printf("[saves] write_save_files: %zu files\n", entries.size());

    int ret = 0;

    for (size_t e = 0; e < entries.size(); e++) {
        const std::string& path = entries[e].first;
        const std::vector<uint8_t>& bytes = entries[e].second;

        if (path.empty()) {
            printf("  write_save_files: missing path in file entry\n");
            ret = -1;
            continue;
        }

        // Create parent directories under save:/ (split path on '/').
        size_t last_slash = path.rfind('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            std::string dir_part = path.substr(0, last_slash);
            std::string accumulated = "save:";
            size_t start = 0;
            while (start < dir_part.size()) {
                size_t end = dir_part.find('/', start);
                if (end == std::string::npos) end = dir_part.size();
                if (end > start) {
                    accumulated += "/" + dir_part.substr(start, end - start);
                    mkdir(accumulated.c_str(), 0755); // ignore if already exists
                }
                start = end + 1;
            }
        }

        std::string full_path = "save:/" + path;
        FILE* f = fopen(full_path.c_str(), "wb");
        if (!f) {
            printf("  write_save_files: fopen failed for %s\n", path.c_str());
            ret = -1;
            continue;
        }
        if (!bytes.empty()) {
            size_t written = fwrite(bytes.data(), 1, bytes.size(), f);
            if (written != bytes.size()) {
                printf("  write_save_files: fwrite short write for %s\n", path.c_str());
                fclose(f);
                ret = -1;
                continue;
            }
        }
        fclose(f);
    }

    // REQUIRED: flush pending writes to the underlying FsFileSystem.
    fsdevCommitDevice("save");
    fsdevUnmountDevice("save");
    return ret;
}
