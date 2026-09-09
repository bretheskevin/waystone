#pragma once
#include "session.h"
#include <string>

struct VaultCreateResult {
    WsVault* vault;            // non-null on success
    std::string recovery_hex;  // recovery key as hex (empty on failure)
    std::string recovery_path; // file path where key was saved
    std::string error;         // non-empty on failure
};

// Create a new vault: overwrite guard, ws_vault_init, write keys.json + config.json.
// session->config.server_url and session->config.username must be set before calling.
VaultCreateResult create_vault(const std::string& passphrase, Session* session);

// After vault creation or unlock: build SyncController and push TitleListActivity.
// session must have vault, uid, device_id, dav fully populated.
void push_dashboard(Session* session);
