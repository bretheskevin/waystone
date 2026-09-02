#include "saves.h"
#include "base64.h"
#include "json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <switch.h>

std::vector<TitleInfo> list_titles() {
    std::vector<TitleInfo> titles;

    if (R_FAILED(nsInitialize())) {
        printf("nsInitialize failed\n");
        return titles;
    }

    NsApplicationRecord records[256];
    s32 offset = 0;

    while (true) {
        s32 got = 0;
        Result rc = nsListApplicationRecord(records, 256, static_cast<s64>(offset), &got);
        if (R_FAILED(rc) || got == 0) break;

        for (s32 i = 0; i < got; i++) {
            TitleInfo info;
            info.title_id = records[i].application_id;

            // Get display name from NACP. ~128 KiB -- heap-allocated.
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
    return titles;
}

// Recursively walk a directory and collect all files as (relative_path, content) pairs.
static void walk_dir(const char* base, const char* rel,
                     std::vector<std::pair<std::string, std::vector<uint8_t>>>* out) {
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
            walk_dir(base, child_rel.c_str(), out);
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
            out->push_back({child_rel, content});
        }
    }
    closedir(d);
}

std::string extract_save_json(const TitleInfo& title, AccountUid uid) {
    // Mount save data
    Result rc = fsdevMountSaveData("save", title.title_id, uid);
    if (R_FAILED(rc)) {
        // FsError_TargetNotFound = 0x7D402 means no save exists for this title+user
        printf("  fsdevMountSaveData failed: 0x%X\n", rc);
        return "";
    }

    // Walk the mounted filesystem
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    walk_dir("save:", "", &files);

    fsdevUnmountDevice("save");

    if (files.empty()) return "";

    // Wrap file paths in JKSV convention: <GameName>/main/<relative_path>
    // This lets ws_jksv_normalize parse title_dir=<GameName>, slot="main".
    std::vector<std::pair<std::string, std::vector<uint8_t>>> wrapped;
    wrapped.reserve(files.size());
    for (auto& f : files) {
        std::string jksv_path = title.name + "/main/" + f.first;
        wrapped.push_back({jksv_path, std::move(f.second)});
    }

    return build_raw_tree_json(wrapped);
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
