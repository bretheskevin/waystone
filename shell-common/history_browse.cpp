#include "history_browse.h"
#include "json.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Forward-declare the FFI symbols needed here. Do NOT include waystone.h —
// the struct Vault forward-decl + typedef in history_browse.h is sufficient,
// and avoids pulling in platform headers. Struct layout matches
// ffi/include/waystone.h exactly.
extern "C" {
    struct WsBuf { uint8_t* ptr; uintptr_t len; };
    WsBuf ws_vault_decrypt_heads(const WsVault* vault, const uint8_t* data, uintptr_t len);
    void ws_buf_free(WsBuf buf);
}

// Parse the {ts} and {device} fields out of a history filename.
// Format: "{ts}-{device}.json" where ts is "YYYYMMDDTHHMMSSZ" (16 chars, no '-')
// and device is a UUID (may itself contain '-'). Returns false if it doesn't match.
static bool parse_entry_filename(const std::string& href,
                                 std::string* ts_out,
                                 std::string* device_out) {
    size_t slash = href.rfind('/');
    std::string fname = (slash != std::string::npos) ? href.substr(slash + 1) : href;

    // Must end with ".json" and be long enough: ts(16) + "-" + device(>=1) + ".json"
    const char* suffix = ".json";
    size_t slen = strlen(suffix);
    if (fname.size() < 16 + 1 + 1 + slen) return false;
    if (fname.compare(fname.size() - slen, slen, suffix) != 0) return false;

    // ts is the first 16 characters (YYYYMMDDTHHMMSSZ), separated from device by '-'
    std::string ts = fname.substr(0, 16);
    if (ts[8] != 'T' || ts[15] != 'Z' || fname[16] != '-') return false;

    // device is everything between the ts separator and the ".json" suffix
    *ts_out = ts;
    *device_out = fname.substr(17, fname.size() - 17 - slen);
    return true;
}

std::vector<HistoryEntry> list_history(const WsVault* vault,
                                       const std::string& base_path,
                                       const WebDavCfg& dav) {
    (void)vault; // heads are decrypted lazily in fetch_history_hash, not here
    std::vector<HistoryEntry> entries;

    std::string history_path = base_path + "/history/";
    std::vector<std::string> hrefs;
    int rc = webdav_propfind(dav, history_path.c_str(), &hrefs);
    if (rc != 0) {
        printf("[history] propfind %s failed (rc=%d)\n", history_path.c_str(), rc);
        return entries;
    }
    if (hrefs.empty()) {
        printf("[history] propfind %s: no entries\n", history_path.c_str());
        return entries;
    }

    for (size_t i = 0; i < hrefs.size(); i++) {
        const std::string& href = hrefs[i];

        // Skip the collection itself (ends with "/" or equals the path)
        if (href == history_path || href.empty()) continue;
        if (href[href.size() - 1] == '/') continue;

        std::string ts, device;
        if (!parse_entry_filename(href, &ts, &device)) continue;

        HistoryEntry entry;
        entry.timestamp = ts;
        entry.device_id = device;
        entry.get_path  = href; // resolved to a hash lazily via fetch_history_hash
        entries.push_back(entry);
    }

    // Sort newest-first (timestamp is fixed-width UTC, string-sorts correctly)
    std::sort(entries.begin(), entries.end(),
              [](const HistoryEntry& a, const HistoryEntry& b) {
                  return a.timestamp > b.timestamp;
              });

    printf("[history] listed %zu version(s) from %s (single propfind)\n",
           entries.size(), history_path.c_str());
    return entries;
}

std::string fetch_history_hash(const WsVault* vault,
                               const HistoryEntry& entry,
                               const WebDavCfg& dav) {
    if (entry.get_path.empty()) {
        printf("[history] fetch_history_hash: empty get_path for ts=%s\n",
               entry.timestamp.c_str());
        return "";
    }
    printf("[history] fetching head %s\n", entry.get_path.c_str());

    std::vector<uint8_t> encrypted;
    int grc = webdav_get(dav, entry.get_path.c_str(), &encrypted);
    if (grc != 0 || encrypted.empty()) {
        printf("[history] head GET failed (rc=%d) %s\n", grc, entry.get_path.c_str());
        return "";
    }

    WsBuf decrypted = ws_vault_decrypt_heads(vault, encrypted.data(), encrypted.size());
    if (!decrypted.ptr || decrypted.len == 0) {
        printf("[history] head decrypt failed %s\n", entry.get_path.c_str());
        ws_buf_free(decrypted);
        return "";
    }

    std::string json_str((const char*)decrypted.ptr, decrypted.len);
    ws_buf_free(decrypted);

    std::string hash = json_get_string(json_str.c_str(), "hash");
    if (hash.empty()) {
        printf("[history] head missing hash field %s\n", entry.get_path.c_str());
        return "";
    }
    printf("[history] resolved hash=%.12s for ts=%s\n", hash.c_str(), entry.timestamp.c_str());
    return hash;
}
