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

// Extract the timestamp prefix from a history filename.
// Format: "{ts}-{device}.json" where ts is "YYYYMMDDTHHMMSSZ" (16 chars).
// Returns empty string if the filename doesn't match.
static std::string parse_ts_from_filename(const std::string& href) {
    size_t slash = href.rfind('/');
    std::string fname = (slash != std::string::npos) ? href.substr(slash + 1) : href;

    // Must end with ".json" and be long enough: ts(16) + "-" + device(>=1) + ".json"
    const char* suffix = ".json";
    size_t slen = strlen(suffix);
    if (fname.size() < 16 + 1 + 1 + slen) return "";
    if (fname.compare(fname.size() - slen, slen, suffix) != 0) return "";

    // ts is the first 16 characters (YYYYMMDDTHHMMSSZ)
    std::string ts = fname.substr(0, 16);
    if (ts[8] != 'T' || ts[15] != 'Z') return "";

    return ts;
}

std::vector<HistoryEntry> list_history(const WsVault* vault,
                                       const std::string& base_path,
                                       const WebDavCfg& dav) {
    std::vector<HistoryEntry> entries;

    std::string history_path = base_path + "/history/";
    std::vector<std::string> hrefs;
    int rc = webdav_propfind(dav, history_path.c_str(), &hrefs);
    if (rc != 0 || hrefs.empty()) return entries;

    for (size_t i = 0; i < hrefs.size(); i++) {
        const std::string& href = hrefs[i];

        // Skip the collection itself (ends with "/" or equals the path)
        if (href == history_path || href.empty()) continue;
        if (href[href.size() - 1] == '/') continue;

        std::string ts = parse_ts_from_filename(href);
        if (ts.empty()) continue;

        std::vector<uint8_t> encrypted;
        int grc = webdav_get(dav, href.c_str(), &encrypted);
        if (grc != 0 || encrypted.empty()) continue;

        WsBuf decrypted = ws_vault_decrypt_heads(vault, encrypted.data(), encrypted.size());
        if (!decrypted.ptr || decrypted.len == 0) {
            ws_buf_free(decrypted);
            continue;
        }

        std::string json_str((const char*)decrypted.ptr, decrypted.len);
        ws_buf_free(decrypted);

        HistoryEntry entry;
        entry.timestamp = ts;
        entry.device_id = json_get_string(json_str.c_str(), "device_id");
        entry.hash      = json_get_string(json_str.c_str(), "hash");
        entry.mtime     = json_get_string(json_str.c_str(), "mtime");

        if (entry.hash.empty()) continue;

        entries.push_back(entry);
    }

    // Sort newest-first (timestamp is fixed-width UTC, string-sorts correctly)
    std::sort(entries.begin(), entries.end(),
              [](const HistoryEntry& a, const HistoryEntry& b) {
                  return a.timestamp > b.timestamp;
              });

    return entries;
}
