#include "saves.h"
#include "base64.h"
#include "json.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <3ds.h>

// SMDH large-icon size: 48×48 pixels × 2 bytes/pixel (RGB565) = 4608 bytes.
static const size_t SMDH_SIZE     = 0x36C0;
static const size_t SMDH_ICON_OFF = 0x24C0;
static const size_t SMDH_ICON_LEN = 0x1200; // 4608 bytes

// Read SMDH metadata for the given title_id via the ExeFS "icon" file.
// On success, writes the real UTF-8 name into `name` and the 4608-byte
// RGB565 tiled icon into `icon`. On any failure, leaves both unchanged
// (caller pre-sets the hex-uid fallback name).
static void read_smdh(u64 tid, std::string& name, std::vector<uint8_t>& icon) {
    // Archive binary path: {low32, high32, mediatype, 0}
    u32 arch_data[4] = {
        static_cast<u32>(tid & 0xFFFFFFFF),
        static_cast<u32>(tid >> 32),
        static_cast<u32>(MEDIATYPE_SD),
        0
    };
    FS_Path arch_path = { PATH_BINARY, sizeof(arch_data), arch_data };

    // File binary path: ExeFS "icon" entry {0, 0, 0x2, 0x6E6F6369}
    u32 file_data[4] = { 0, 0, 0x2, 0x6E6F6369 };
    FS_Path file_path = { PATH_BINARY, sizeof(file_data), file_data };

    Handle fh = 0;
    Result rc = FSUSER_OpenFileDirectly(
        &fh,
        ARCHIVE_SAVEDATA_AND_CONTENT,
        arch_path,
        file_path,
        FS_OPEN_READ,
        0
    );
    if (R_FAILED(rc)) {
        printf("[titles] smdh open failed tid=%016llX rc=0x%08lX (name/icon fallback)\n",
               (unsigned long long)tid, (unsigned long)rc);
        return;
    }

    uint8_t smdh[SMDH_SIZE];
    u32 bytes_read = 0;
    rc = FSFILE_Read(fh, &bytes_read, 0, smdh, static_cast<u32>(SMDH_SIZE));
    FSFILE_Close(fh);

    if (R_FAILED(rc) || bytes_read < static_cast<u32>(SMDH_SIZE)) {
        printf("[titles] smdh read failed tid=%016llX rc=0x%08lX bytes=%lu (name/icon fallback)\n",
               (unsigned long long)tid, (unsigned long)rc, (unsigned long)bytes_read);
        return;
    }

    // Verify "SMDH" magic at offset 0.
    if (memcmp(smdh, "SMDH", 4) != 0) {
        printf("[titles] smdh bad magic tid=%016llX (name/icon fallback)\n",
               (unsigned long long)tid);
        return;
    }

    // Extract the short description (first 0x40 UTF-16LE code units) from the
    // English title struct (index 1), then Japanese (index 0), then any non-empty.
    // Each title struct is 0x200 bytes; the short desc is the first 0x80 bytes.
    std::string resolved_name;
    int lang_order[] = { 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    for (int li = 0; li < 16 && resolved_name.empty(); li++) {
        int lang = lang_order[li];
        const uint16_t* utf16 = reinterpret_cast<const uint16_t*>(
            smdh + 0x0008 + lang * 0x200
        );
        // Max UTF-8 output: 0x40 code units × 3 bytes each + null.
        uint8_t utf8_buf[0x40 * 3 + 1];
        memset(utf8_buf, 0, sizeof(utf8_buf));
        ssize_t written = utf16_to_utf8(utf8_buf, utf16, sizeof(utf8_buf) - 1);
        if (written <= 0) continue;
        utf8_buf[written] = '\0';
        // Trim trailing whitespace and null bytes.
        int end = static_cast<int>(written) - 1;
        while (end >= 0 && (utf8_buf[end] == 0 || utf8_buf[end] == ' ' ||
                             utf8_buf[end] == '\t' || utf8_buf[end] == '\r' ||
                             utf8_buf[end] == '\n')) {
            end--;
        }
        if (end >= 0) {
            resolved_name = std::string(reinterpret_cast<const char*>(utf8_buf),
                                        static_cast<size_t>(end + 1));
        }
    }

    if (!resolved_name.empty()) {
        name = resolved_name;
    }

    // Copy the 48×48 large icon RGB565 tiled data.
    icon.assign(smdh + SMDH_ICON_OFF, smdh + SMDH_ICON_OFF + SMDH_ICON_LEN);

    printf("[titles] smdh tid=%016llX name='%s' icon=%zuB\n",
           (unsigned long long)tid, name.c_str(), icon.size());
}

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

    int icons_found = 0;
    for (u32 i = 0; i < read; i++) {
        TitleInfo info;
        info.title_id = title_ids[i];
        info.unique_id = (info.title_id >> 8) & 0xFFFFF;

        // Default name: hex uniqueID (overwritten by read_smdh on success).
        char hex_uid[8];
        snprintf(hex_uid, sizeof(hex_uid), "%05X", info.unique_id);
        info.name = hex_uid;

        read_smdh(info.title_id, info.name, info.icon);
        if (!info.icon.empty()) icons_found++;

        titles.push_back(info);
    }

    printf("[titles] %zu titles (%d with icons)\n", titles.size(), icons_found);

    amExit();
    return titles;
}

// Open the ARCHIVE_USER_SAVEDATA for a given title_id (SD card).
// Caller must FSUSER_CloseArchive on success.
static Result open_save_archive(u64 title_id, FS_Archive* archive) {
    u32 path_data[3] = {MEDIATYPE_SD,
                        static_cast<u32>(title_id & 0xFFFFFFFF),
                        static_cast<u32>(title_id >> 32)};
    FS_Path archive_path = {PATH_BINARY, sizeof(path_data), path_data};
    return FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, archive_path);
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
    FS_Archive archive;
    if (R_FAILED(open_save_archive(title.title_id, &archive))) {
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

uint8_t* read_keys_file(const char* path, long* len_out) {
    *len_out = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return nullptr; }
    uint8_t* buf = static_cast<uint8_t*>(malloc(static_cast<size_t>(len)));
    if (!buf) { fclose(f); return nullptr; }
    size_t got = fread(buf, 1, static_cast<size_t>(len), f);
    fclose(f);
    if (got != static_cast<size_t>(len)) { free(buf); return nullptr; }
    *len_out = len;
    return buf;
}

int write_save_files(u64 title_id, const char* files_json) {
    FS_Archive archive;
    if (R_FAILED(open_save_archive(title_id, &archive))) {
        printf("  write_save_files: FSUSER_OpenArchive failed for TID %016llX\n",
               (unsigned long long)title_id);
        return -1;
    }

    // Wipe the archive root before writing so stale files from a previous
    // backup do not survive (mirrors Checkpoint's DeleteDirectoryRecursively
    // before copyTree). Non-fatal: an empty/fresh archive may return an error.
    Result del_res = FSUSER_DeleteDirectoryRecursively(archive,
                         fsMakePath(PATH_ASCII, "/"));
    if (R_FAILED(del_res)) {
        printf("  write_save_files: DeleteDirectoryRecursively returned 0x%08lX (non-fatal)\n",
               (unsigned long)del_res);
    }

    std::vector<std::string> entries = json_split_array(files_json);
    int ret = 0;

    for (const auto& entry_str : entries) {
        std::string path     = json_get_string(entry_str.c_str(), "path");
        std::string data_b64 = json_get_string(entry_str.c_str(), "data_b64");

        if (path.empty()) {
            printf("  write_save_files: missing path in file entry\n");
            ret = -1;
            continue;
        }

        // Decode file content (empty data_b64 → 0-byte file, which is valid).
        std::vector<uint8_t> bytes;
        if (!data_b64.empty()) {
            bytes = base64_decode(data_b64);
            if (bytes.empty()) {
                printf("  write_save_files: base64_decode failed for %s\n", path.c_str());
                ret = -1;
                continue;
            }
        }

        // Create parent directories under the archive root.
        size_t last_slash = path.rfind('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            std::string dir_part = path.substr(0, last_slash);
            std::string accumulated;
            size_t start = 0;
            while (start < dir_part.size()) {
                size_t end = dir_part.find('/', start);
                if (end == std::string::npos) end = dir_part.size();
                if (end > start) {
                    accumulated += "/" + dir_part.substr(start, end - start);
                    FSUSER_CreateDirectory(archive,
                        fsMakePath(PATH_ASCII, accumulated.c_str()), 0);
                    // ignore result — already-exists is expected and OK
                }
                start = end + 1;
            }
        }

        // Open (or create) the file for writing.
        Handle fh;
        std::string file_path = "/" + path;
        if (R_FAILED(FSUSER_OpenFile(&fh, archive,
                     fsMakePath(PATH_ASCII, file_path.c_str()),
                     FS_OPEN_CREATE | FS_OPEN_WRITE, 0))) {
            printf("  write_save_files: FSUSER_OpenFile failed for %s\n", path.c_str());
            ret = -1;
            continue;
        }

        // Truncate to exact size (FS_OPEN_WRITE does not truncate unlike fopen "wb").
        if (R_FAILED(FSFILE_SetSize(fh, static_cast<u64>(bytes.size())))) {
            printf("  write_save_files: FSFILE_SetSize failed for %s\n", path.c_str());
            FSFILE_Close(fh);
            ret = -1;
            continue;
        }

        if (!bytes.empty()) {
            u32 written = 0;
            if (R_FAILED(FSFILE_Write(fh, &written, 0,
                         bytes.data(), static_cast<u32>(bytes.size()),
                         FS_WRITE_FLUSH))) {
                printf("  write_save_files: FSFILE_Write failed for %s\n", path.c_str());
                FSFILE_Close(fh);
                ret = -1;
                continue;
            }
        }
        FSFILE_Close(fh);
    }

    // Do NOT commit a partial restore — a half-written committed save is worse
    // than a failed restore (mirrors Checkpoint aborting on first copy failure).
    if (ret != 0) {
        FSUSER_CloseArchive(archive);
        return -1;
    }

    // Flush all pending writes to the underlying save filesystem.
    if (R_FAILED(FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA,
                                       NULL, 0, NULL, 0))) {
        printf("  write_save_files: ARCHIVE_ACTION_COMMIT_SAVE_DATA failed\n");
        FSUSER_CloseArchive(archive);
        return -1;
    }
    FSUSER_CloseArchive(archive);

    // Delete the console secure value so the game regenerates a fresh one
    // instead of rejecting the restored save. Some titles store an anti-tamper
    // value outside the archive; after overwriting the save it no longer
    // matches. Mirrors Checkpoint's !isTwl block in io::restore(). Use raw
    // title_id & 0xFFFFFF00ULL (NOT unique_id/0xFFFFF) to address the correct
    // slot. Non-fatal: titles with no secure value error on delete.
    u8 sv_out = 0;
    u64 secure_value = ((u64)SECUREVALUE_SLOT_SD << 32) | (u64)(title_id & 0xFFFFFF00ULL);
    Result sv = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE,
                    &secure_value, sizeof(secure_value),
                    &sv_out, sizeof(sv_out));
    if (R_FAILED(sv)) {
        printf("  write_save_files: ControlSecureSave delete returned 0x%08lX (non-fatal)\n",
               (unsigned long)sv);
    }

    return 0;
}
