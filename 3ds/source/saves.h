#ifndef WAYSTONE_3DS_SAVES_H
#define WAYSTONE_3DS_SAVES_H

#include <cstdint>
#include <string>
#include <vector>
#include <3ds.h>

struct TitleInfo {
    u64 title_id;
    u32 unique_id; // (title_id >> 8) & 0xFFFFF
    std::string name;
};

// Enumerate installed SD titles via AM service.
std::vector<TitleInfo> list_titles();

// Extract savedata for a title as a RawTreeDto JSON string.
// Paths formatted for ws_checkpoint_normalize:
//   "0x<5-hex uniqueID> <name>/main/<relative_file_path>"
// No uid parameter -- 3DS savedata is per-title, not per-user.
std::string extract_save_json(const TitleInfo& title);

// Get current UTC time as ISO 8601 string.
std::string current_utc_time();

// Get or create a persistent device ID (stored at sdmc:/waystone/device_id.txt).
std::string get_device_id();

#endif
