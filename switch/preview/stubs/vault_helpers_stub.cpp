/*
 * Preview stub for vault_helpers — replaces switch/source/ui/vault_helpers.cpp.
 * Avoids dragging in SyncController, TitleListActivity, and Waystone FFI from
 * the real implementation.  Just enough to let the wizard compile and render.
 */
#include "vault_helpers.h"
#include <borealis.hpp>

// Forward-declare Vault so the return type compiles without the full FFI header.
struct Vault { int _pad; };

// Static backing storage for the fake vault pointer — never actually freed.
static Vault g_preview_vault;

VaultCreateResult create_vault(const std::string& passphrase, Session* session)
{
    (void)passphrase;
    (void)session;

    VaultCreateResult r;
    r.vault        = &g_preview_vault;
    r.recovery_hex = "DEAD0001DEAD0002DEAD0003DEAD0004"
                     "DEAD0005DEAD0006DEAD0007DEAD0008";
    r.recovery_path = "/preview/recovery.txt";
    r.error         = "";
    return r;
}

void push_dashboard(Session* session)
{
    (void)session;
    // No-op in preview — we never want to spin up a real SyncController.
    // If called from RecoveryKeyActivity's "I've saved it" button, do nothing
    // so the app just keeps showing the recovery screen.
}
