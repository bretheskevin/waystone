/*
 * Preview stub for vault_helpers — replaces switch/source/ui/vault_helpers.cpp.
 * Avoids dragging in SyncController, TitleListActivity, net and the vault FFI from
 * the real implementation.  Just enough to let the wizard compile and render.
 */
#include "vault_helpers.h"
#include <borealis.hpp>

VaultSetupResult setup_create_vault(Session*, const WebDavCfg&, const std::string&) {
    return {reinterpret_cast<WsVault*>(1),
            "DEAD0001DEAD0002DEAD0003DEAD0004DEAD0005DEAD0006DEAD0007DEAD0008", {}, ""};
}

VaultSetupResult setup_join_vault(Session*, const std::vector<uint8_t>&, const std::string&,
                                  bool recovery) {
    return {nullptr, "", {}, recovery ? "Wrong recovery key" : "Wrong passphrase"};
}

std::vector<TitleInfo> load_titles_after_unlock(Session*) { return {}; }

// No-op in preview — we never want to spin up a real SyncController.
void push_dashboard(Session*) {}
