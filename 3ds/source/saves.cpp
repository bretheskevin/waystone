#include "saves.h"
#include "base64.h"
#include "json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

std::vector<TitleInfo> list_titles() {
    std::vector<TitleInfo> titles;

    if (R_FAILED(amInit())) {
        printf("amInit failed\n");
        return titles;
    }

    u32 count = 0;
    if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) || count == 0) {
        amExit();
        return titles;
    }

    std::vector<u64> title_ids(count);
    u32 read = 0;
    if (R_FAILED(AM_GetTitleList(&read, MEDIATYPE_SD, count, title_ids.data()))) {
        amExit();
        return titles;
    }

    for (u32 i = 0; i < read; i++) {
        TitleInfo info;
        info.title_id = title_ids[i];
        info.unique_id = (info.title_id >> 8) & 0xFFFFF;

        // Default name: hex uniqueID
        char hex_uid[8];
        snprintf(hex_uid, sizeof(hex_uid), "%05X", info.unique_id);
        info.name = hex_uid;

        // Try SMDH short title via AM_GetTitleInfo
        AM_TitleEntry entry;
        if (R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &info.title_id, &entry))) {
            // AM_TitleEntry carries size but not the display name directly.
            // SMDH must be read from the title's content. Hex uniqueID fallback
            // is acceptable per the spec; SMDH names are a non-goal.
        }

        titles.push_back(info);
    }

    amExit();
    return titles;
}

// Recursively walk a 3DS save archive directory using libctru FS API.
static void walk_archive(FS_Archive archive, const char* rel,
                         std::vector<std::pair<std::string, std::vector<uint8_t>>>* out) {
    Handle dir;
    std::string dir_path = std::string("/") + rel;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, archive,
                 fsMakePath(PATH_ASCII, dir_path.c_str())))) {
        return;
    }

    FS_DirectoryEntry entry;
    u32 entries_read = 0;

    while (true) {
        entries_read = 0;
        if (R_FAILED(FSDIR_Read(dir, &entries_read, 1, &entry)) || entries_read == 0)
            break;

        // Convert UTF-16 name to ASCII (sufficient for save file names)
        char name[256];
        int j = 0;
        for (int k = 0; k < 256 && entry.name[k] != 0; k++) {
            if (entry.name[k] < 128)
                name[j++] = static_cast<char>(entry.name[k]);
        }
        name[j] = '\0';

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        std::string child_rel;
        if (rel[0] != '\0')
            child_rel = std::string(rel) + "/" + name;
        else
            child_rel = name;

        if (entry.attributes & FS_ATTRIBUTE_DIRECTORY) {
            walk_archive(archive, child_rel.c_str(), out);
        } else {
            Handle file;
            std::string file_path = std::string("/") + child_rel;
            if (R_FAILED(FSUSER_OpenFile(&file, archive,
                         fsMakePath(PATH_ASCII, file_path.c_str()),
                         FS_OPEN_READ, 0))) {
                continue;
            }

            u64 file_size = 0;
            FSFILE_GetSize(file, &file_size);

            std::vector<uint8_t> content(static_cast<size_t>(file_size));
            if (file_size > 0) {
                u32 bytes_read = 0;
                if (R_FAILED(FSFILE_Read(file, &bytes_read, 0,
                             content.data(), static_cast<u32>(file_size)))) {
                    FSFILE_Close(file);
                    continue;
                }
                if (bytes_read != static_cast<u32>(file_size)) {
                    FSFILE_Close(file);
                    continue;
                }
            }
            FSFILE_Close(file);
            out->push_back({child_rel, content});
        }
    }

    FSDIR_Close(dir);
}

std::string extract_save_json(const TitleInfo& title) {
    // Open the title's savedata archive (SD card titles).
    FS_Archive archive;
    u32 path_data[3] = {MEDIATYPE_SD,
                        static_cast<u32>(title.title_id & 0xFFFFFFFF),
                        static_cast<u32>(title.title_id >> 32)};
    FS_Path archive_path = {PATH_BINARY, sizeof(path_data), path_data};

    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_USER_SAVEDATA, archive_path))) {
        printf("  FSUSER_OpenArchive failed for TID %016llX\n",
               (unsigned long long)title.title_id);
        return "";
    }

    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    walk_archive(archive, "", &files);

    FSUSER_CloseArchive(archive);

    if (files.empty()) return "";

    // Wrap file paths in Checkpoint convention:
    //   "0x<5-hex uniqueID> <Name>/main/<relative_path>"
    char hex_uid[8];
    snprintf(hex_uid, sizeof(hex_uid), "%05X", title.unique_id);
    std::string checkpoint_dir = std::string("0x") + hex_uid + " " + title.name;

    std::vector<std::pair<std::string, std::vector<uint8_t>>> wrapped;
    wrapped.reserve(files.size());
    for (auto& f : files) {
        std::string path = checkpoint_dir + "/main/" + f.first;
        wrapped.push_back({path, std::move(f.second)});
    }

    return build_raw_tree_json(wrapped);
}

std::string current_utc_time() {
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return buf;
}

std::string get_device_id() {
    const char* path = "sdmc:/waystone/device_id.txt";
    const char* dir = "sdmc:/waystone";

    FILE* f = fopen(path, "r");
    if (f) {
        char buf[65] = {};
        size_t n = fread(buf, 1, 64, f);
        fclose(f);
        if (n >= 16) return std::string(buf, n);
    }

    // Generate new random ID (32 hex chars = 16 random bytes).
    // psInit() must have been called in main() before this point.
    uint8_t rand_bytes[16];
    PS_GenerateRandomBytes(rand_bytes, sizeof(rand_bytes));
    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", rand_bytes[i]);
    }

    mkdir(dir, 0755);
    f = fopen(path, "w");
    if (f) {
        fwrite(hex, 1, 32, f);
        fclose(f);
    }

    return std::string(hex, 32);
}
