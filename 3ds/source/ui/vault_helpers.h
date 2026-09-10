#pragma once
#include "session.h"
#include <string>

struct VaultCreateResult {
    WsVault* vault;             // non-null on success
    std::string recovery_hex;   // recovery key as hex (empty on failure)
    std::string recovery_path;  // file path where key was saved
    std::string error;          // non-empty on failure
    VaultCreateResult() : vault(0) {}
};

// Overwrite guard, ws_vault_init, write keys.json + config.json + recovery file.
// session->config.server_url and session->config.username must be set before calling.
// HEAVY (Argon2 KDF): call from a worker thread, never the render thread.
VaultCreateResult create_vault(const std::string& passphrase, Session* session);
