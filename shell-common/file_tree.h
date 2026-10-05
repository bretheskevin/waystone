#ifndef WAYSTONE_FILE_TREE_H
#define WAYSTONE_FILE_TREE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// A decoded file: (path, content bytes).
typedef std::pair<std::string, std::vector<uint8_t> > FileTreeEntry;

// Encode an ordered (path, bytes) list as a WsFileTree buffer (little-endian).
std::vector<uint8_t> file_tree_encode(const std::vector<FileTreeEntry>& files);

// Decode a WsFileTree buffer. Returns false on truncation / length overflow.
// UTF-8 is NOT validated (paths are kept as raw bytes). On false, *out is left
// empty. No out-of-bounds reads.
bool file_tree_decode(const uint8_t* buf, size_t len,
                      std::vector<FileTreeEntry>* out);

// One entry of a WsSaveList: metadata JSON string + a slice (ALIASING `buf`
// passed to save_list_decode) of that save's inline WsFileTree.
struct SaveListEntry {
    std::string meta_json;
    const uint8_t* files_ptr; // points inside the buffer passed to save_list_decode
    size_t files_len;
};

// Decode a WsSaveList buffer. `buf` MUST outlive `*out` (files_ptr aliases it).
// Returns false on malformed input; *out left empty.
bool save_list_decode(const uint8_t* buf, size_t len,
                      std::vector<SaveListEntry>* out);

#endif
