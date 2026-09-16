#ifndef WAYSTONE_HISTORY_BROWSE_H
#define WAYSTONE_HISTORY_BROWSE_H

#include "browse_phase.h"
#include "net.h"
#include <string>
#include <vector>

struct Vault;
typedef Vault WsVault;

struct HistoryEntry {
    std::string timestamp;   // extracted from filename "{ts}-{device}.json"
    std::string device_id;   // decrypted from the entry JSON
    std::string hash;        // decrypted from the entry JSON
    std::string mtime;       // decrypted from the entry JSON
};

// List remote history entries for a save, newest-first.
// base_path: the obfuscated remote base path for the save (from SaveDecision.base_path).
// Steps: PROPFIND {base_path}/history/ -> for each href: GET -> ws_vault_decrypt_heads
//        -> parse ts from filename -> sort newest-first.
// Returns empty vector if the history/ dir is empty or does not exist.
std::vector<HistoryEntry> list_history(const WsVault* vault,
                                       const std::string& base_path,
                                       const WebDavCfg& dav);

#endif
