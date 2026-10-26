#ifndef WAYSTONE_KEYS_FILE_H
#define WAYSTONE_KEYS_FILE_H
#include <cstdint>
#include <string>

// Console SD layout (Switch + 3DS): the local copy of the vault's keys.json and the recovery
// key file written at vault creation.
static const char* const WS_KEYS_PATH = "sdmc:/waystone/keys.json";

inline std::string ws_recovery_path(const std::string& device_id) {
    return "sdmc:/waystone/recovery-" + device_id + ".txt";
}

// Read a whole file into a malloc'd buffer (caller frees with free()). Returns nullptr and
// *len_out = 0 on missing/empty/short-read.
uint8_t* read_keys_file(const char* path, long* len_out);

#endif
