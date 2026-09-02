#ifndef WAYSTONE_SAVES_H
#define WAYSTONE_SAVES_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <switch.h>

struct TitleInfo {
    uint64_t title_id;
    std::string name; // display name from NACP, or hex TID fallback
};

// Enumerate installed titles. Initializes/exits ns internally.
// Returns empty vector on failure (prints error to console).
std::vector<TitleInfo> list_titles();

// Extract save data for a title+user as a RawTreeDto JSON string.
// The returned JSON has paths formatted for ws_jksv_normalize:
//   "<game_name>/<slot>/<relative_file_path>"
// where slot is "main" (primary save extraction).
// uid: the user account UID.
// Returns empty string on failure (e.g. no save exists).
std::string extract_save_json(const TitleInfo& title, AccountUid uid);

// Get the current account UID. Initializes/exits account internally.
// Returns true on success, false on failure.
bool get_active_account(AccountUid* out_uid);

// Get current UTC time as ISO 8601 string (e.g. "2026-09-02T15:30:00Z").
std::string current_utc_time();

// Get or create a persistent device ID (stored at sdmc:/waystone/device_id.txt).
// Generates a random 32-hex-char ID on first call using randomGet.
// Returns empty string on failure.
std::string get_device_id();

// Restore save files from a flat FileEntryDto JSON array produced by ws_unzip.
// files_json: JSON array of {"path":"...","data_b64":"..."} — paths are
//   save-relative (written directly under save:/).
// Mounts save data (read-write), writes each file, commits with
// fsdevCommitDevice (REQUIRED or writes are lost), then unmounts.
// Returns 0 on success, -1 if mount fails.
int write_save_files(u64 title_id, AccountUid uid, const char* files_json);

#endif
