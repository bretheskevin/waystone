#ifndef WAYSTONE_JSON_H
#define WAYSTONE_JSON_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// ---- JSON generation ----

// Escape a string for embedding in a JSON string value (handles \\ \" \b \f \n \r \t
// and control chars U+0000..U+001F).
std::string json_escape(const std::string& s);

// Build a RawTreeDto JSON object from extracted files.
// Each pair is (relative_path, file_content_bytes).
// Output: {"files":[{"path":"<escaped>","data_b64":"<base64>"},...]}'
std::string build_raw_tree_json(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);

// Build a DeviceHead JSON object.
// Output: {"device_id":"...","hash":"...","mtime":"..."}
std::string build_device_head_json(const char* device_id,
                                   const char* hash,
                                   const char* mtime);

// Replace the empty mtime field in a NormalizedSaveDto JSON string.
// The adapter (ws_jksv_normalize) produces compact JSON with "mtime":"".
// This replaces the FIRST occurrence of "mtime":"" with "mtime":"<mtime>".
// Returns the modified JSON string, or the original if no match found.
std::string json_set_mtime(const std::string& json, const char* mtime);

// ---- JSON parsing (jsmn wrappers) ----

// Get the value of a top-level string field. Returns "" if not found.
std::string json_get_string(const char* json, const char* key);

// Get a string field nested one level: json.outer.inner. Returns "" if not found.
std::string json_get_nested_string(const char* json,
                                   const char* outer_key,
                                   const char* inner_key);

// Split a top-level JSON array into raw JSON substrings for each element.
// Returns empty vector on parse error or if json is not an array.
std::vector<std::string> json_split_array(const char* json);

#endif
