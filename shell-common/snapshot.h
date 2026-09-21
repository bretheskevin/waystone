#ifndef WAYSTONE_SNAPSHOT_H
#define WAYSTONE_SNAPSHOT_H

#include <string>

// Sanitize a group_key for use as a filesystem path component.
// Replaces '/', '\\', and ".." sequences with '_'.
std::string snapshot_sanitize_key(const std::string& key);

// Write a safety snapshot from a RawTree JSON string
// (format: {"files":[{"path":"...","data_b64":"..."},...]} as emitted by extract_save_json)
// to backup_dir, creating all parent directories.
// Returns true on success, or if the files array is empty (nothing to back up).
// Returns false on any I/O or parse failure — caller must skip the restore.
bool write_snapshot(const char* backup_dir, const char* files_json);

// Number of safety snapshots retained per game key when pruning.
static const int SNAPSHOT_KEEP = 10;

// Keep only the `keep` newest snapshot directories under backup_root
// (e.g. "sdmc:/waystone/backups/<sanitized key>/"). Dir names are
// history_timestamp() format "YYYYMMDDTHHMMSSZ" — lexicographic order is
// chronological. Deletes oldest beyond `keep` (clamped to >= 1 so the
// just-written snapshot is never removed). Entries not matching the strict
// "YYYYMMDDTHHMMSSZ" pattern are neither counted toward keep nor deleted.
// Returns false if any delete failed; callers treat failure as non-fatal
// (log + continue).
bool snapshot_prune(const char* backup_root, int keep);

#endif
