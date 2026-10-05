#include "file_tree.h"

#include <cstring>

// ---- encode ----

static void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void put_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; i++) out.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}

std::vector<uint8_t> file_tree_encode(const std::vector<FileTreeEntry>& files) {
    std::vector<uint8_t> out;
    put_u32(out, (uint32_t)files.size());
    for (size_t i = 0; i < files.size(); i++) {
        const std::string& path = files[i].first;
        const std::vector<uint8_t>& data = files[i].second;
        put_u32(out, (uint32_t)path.size());
        out.insert(out.end(), path.begin(), path.end());
        put_u64(out, (uint64_t)data.size());
        out.insert(out.end(), data.begin(), data.end());
    }
    return out;
}

// ---- decode (bounds-checked cursor) ----

namespace {
struct Cursor {
    const uint8_t* buf;
    size_t len;
    size_t pos;
    Cursor(const uint8_t* b, size_t l) : buf(b), len(l), pos(0) {}

    // Returns a pointer to `n` bytes and advances, or false if out of range.
    // Compared as uint64_t BEFORE any size_t cast (size_t is 32-bit on 3DS).
    bool take(uint64_t n, const uint8_t** out) {
        uint64_t rem = (uint64_t)(len - pos);
        if (n > rem) return false;
        *out = buf + pos;
        pos += (size_t)n; // safe: n <= rem <= len <= SIZE_MAX
        return true;
    }
    bool u32(uint32_t* out) {
        const uint8_t* p;
        if (!take(4, &p)) return false;
        *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        return true;
    }
    bool u64(uint64_t* out) {
        const uint8_t* p;
        if (!take(8, &p)) return false;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
        *out = v;
        return true;
    }
};
} // namespace

static bool decode_tree_at(Cursor& c, std::vector<FileTreeEntry>* out) {
    uint32_t count;
    if (!c.u32(&count)) return false;
    out->clear();
    // Each entry needs >= 12 bytes; cap reserve so a hostile count cannot
    // trigger a huge allocation (exceptions are disabled).
    if ((uint64_t)count <= (uint64_t)(c.len - c.pos) / 12) out->reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t path_len;
        if (!c.u32(&path_len)) return false;
        const uint8_t* pp;
        if (!c.take((uint64_t)path_len, &pp)) return false;
        std::string path((const char*)pp, (size_t)path_len);
        uint64_t data_len;
        if (!c.u64(&data_len)) return false;
        const uint8_t* dp;
        if (!c.take(data_len, &dp)) return false;
        out->push_back(FileTreeEntry(path, std::vector<uint8_t>(dp, dp + (size_t)data_len)));
    }
    return true;
}

bool file_tree_decode(const uint8_t* buf, size_t len, std::vector<FileTreeEntry>* out) {
    out->clear();
    if (buf == 0 && len != 0) return false;
    Cursor c(buf, len);
    if (!decode_tree_at(c, out)) {
        out->clear();
        return false;
    }
    return true;
}

bool save_list_decode(const uint8_t* buf, size_t len, std::vector<SaveListEntry>* out) {
    out->clear();
    if (buf == 0 && len != 0) return false;
    Cursor c(buf, len);
    uint32_t count;
    if (!c.u32(&count)) return false;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t meta_len;
        if (!c.u32(&meta_len)) { out->clear(); return false; }
        const uint8_t* mp;
        if (!c.take((uint64_t)meta_len, &mp)) { out->clear(); return false; }
        SaveListEntry e;
        e.meta_json.assign((const char*)mp, (size_t)meta_len);
        // The inline WsFileTree starts at the current position; validate it by
        // decoding (discarded) while recording the slice, then advance past it.
        size_t tree_start = c.pos;
        std::vector<FileTreeEntry> tmp;
        if (!decode_tree_at(c, &tmp)) { out->clear(); return false; }
        e.files_ptr = buf + tree_start;
        e.files_len = c.pos - tree_start;
        out->push_back(e);
    }
    return true;
}
