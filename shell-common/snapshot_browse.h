#ifndef WAYSTONE_SNAPSHOT_BROWSE_H
#define WAYSTONE_SNAPSHOT_BROWSE_H

#include <string>
#include <vector>

struct SnapshotEntry {
    std::string timestamp;          // dir name, history_timestamp() format "YYYYMMDDTHHMMSSZ"
    std::string path;               // absolute sdmc path to the snapshot dir
    size_t file_count;
    unsigned long long total_bytes;
};

#include "browse_phase.h"

// Port of core/src/model.rs:120 normalize_game_name.
// Keep only ASCII alphanumeric, lowercased.
std::string normalize_game_name(const std::string& name);

// Compute the sanitized backup-dir name for a title on a given system.
// = snapshot_sanitize_key("<system>/" + normalize_game_name(game_name) + "/main")
std::string snapshot_key_dir(const char* system, const std::string& game_name);

// List a title's snapshots newest-first.
// backups_root: e.g. "sdmc:/waystone/backups"
// key_dir: the sanitized backup-dir name (from snapshot_key_dir).
// Returns empty vector if the directory doesn't exist.
std::vector<SnapshotEntry> list_snapshots(const char* backups_root, const char* key_dir);

// Read a snapshot dir back into a FLAT files_json array string:
//   [{"path":"<save-relative>","data_b64":"..."},...]
// Strips the leading two path components (<title_dir>/<slot>/) from each
// file's relative path, producing the shape write_save_files consumes.
// Returns "[]" if the snapshot dir is empty or missing.
// Returns empty string on I/O error.
std::string snapshot_to_flat_files_json(const char* snapshot_dir, const char* slot_filter = nullptr);

// Format bytes as human-readable size string. Mirrors desktop helpers.rs human_size.
std::string human_size(unsigned long long bytes);

// UTC timestamp for snapshot dir names: "YYYYMMDDTHHMMSSZ".
// Shared definition used by both console sync and snapshot-restore code paths.
std::string history_timestamp();

// Reformat "YYYYMMDDTHHMMSS[.fff]Z" as "YYYY-MM-DD HH:MM" for display.
// Render-only: the compact form is kept for storage and lexicographic sorting.
// Unknown shapes are returned unchanged.
std::string human_timestamp(const std::string& ts);

#endif
