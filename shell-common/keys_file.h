#ifndef WAYSTONE_KEYS_FILE_H
#define WAYSTONE_KEYS_FILE_H
#include <cstdint>

// Read a whole file into a malloc'd buffer (caller frees with free()). Returns nullptr and
// *len_out = 0 on missing/empty/short-read.
uint8_t* read_keys_file(const char* path, long* len_out);

#endif
