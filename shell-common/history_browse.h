#ifndef WAYSTONE_HISTORY_BROWSE_H
#define WAYSTONE_HISTORY_BROWSE_H

#include "browse_phase.h"
#include "net.h"
#include <string>
#include <vector>

struct Vault;
typedef Vault WsVault;

struct HistoryEntry {
    std::string timestamp;   // parsed from filename "{ts}-{device}.json"
    std::string device_id;   // parsed from filename "{ts}-{device}.json"
    std::string get_path;    // WebDAV path of this entry's head, for the lazy hash fetch
    std::string hash;        // empty until resolved lazily via fetch_history_hash
};

// List remote history entries for a save, newest-first.
// base_path: the obfuscated remote base path for the save (from SaveDecision.base_path).
// Steps: PROPFIND {base_path}/history/ -> parse {ts} and {device} from each filename
//        -> sort newest-first. Costs a SINGLE PROPFIND: the per-entry head GET+decrypt
//        that resolves `hash` is deferred to fetch_history_hash (call it at restore time),
//        so listing stays fast regardless of how many versions exist.
// Returns empty vector if the history/ dir is empty or does not exist.
std::vector<HistoryEntry> list_history(const WsVault* vault,
                                       const std::string& base_path,
                                       const WebDavCfg& dav);

// Resolve one entry's content hash: GET entry.get_path -> ws_vault_decrypt_heads ->
// parse "hash" from the head JSON. Deferred from list_history so listing costs a single
// PROPFIND. Returns the hash, or empty string on failure (logged).
std::string fetch_history_hash(const WsVault* vault,
                               const HistoryEntry& entry,
                               const WebDavCfg& dav);

#endif
