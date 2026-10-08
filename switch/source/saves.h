#ifndef WAYSTONE_SAVES_H
#define WAYSTONE_SAVES_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <switch.h>

struct TitleInfo {
    uint64_t title_id;
    std::string name;      // display name from NACP, or hex TID fallback
    std::string icon_path; // path to cached icon JPEG on sdmc (empty if none)
};

// Enumerate installed titles. Initializes/exits ns internally.
// Returns empty vector on failure (prints error to console).
std::vector<TitleInfo> list_titles();

// Extract save data for a title+user as a WsFileTree buffer (binary file-tree).
// Paths follow the JKSV convention "<game_name>/main/<relative_file_path>" so
// ws_jksv_normalize can parse title_dir=<game_name>, slot="main".
// Returns an empty vector on failure / no save.
// local_mtime (optional): newest file mtime of the mounted save as ISO-8601 UTC, '' when unusable.
std::vector<uint8_t> extract_save_json(const TitleInfo& title, AccountUid uid,
                                       std::string* local_mtime = nullptr);

// Get the current account UID. Initializes/exits account internally.
// Returns true on success, false on failure.
bool get_active_account(AccountUid* out_uid);

// Get current UTC time as ISO 8601 string (e.g. "2026-09-02T15:30:00Z").
std::string current_utc_time();

// Get or create a persistent device ID (stored at sdmc:/waystone/device_id.txt).
// Generates a random 32-hex-char ID on first call using randomGet.
// Returns empty string on failure.
std::string get_device_id();

// Restore save files from a WsFileTree buffer (from ws_unzip or
// snapshot_to_file_tree). Paths are save-relative (written under save:/).
// Mounts save data (read-write), writes each file, commits with
// fsdevCommitDevice (REQUIRED or writes are lost), then unmounts.
// Returns 0 on success, -1 on decode/mount/write failure.
int write_save_files(u64 title_id, AccountUid uid,
                     const uint8_t* ft_ptr, size_t ft_len);

#endif
