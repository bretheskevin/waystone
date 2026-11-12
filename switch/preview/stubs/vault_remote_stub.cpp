// Preview stub for shell-common/vault_remote.cpp: the setup probe answers from a fixture the
// mode sets (g_preview_probe_state / g_preview_probe_status / g_preview_probe_plain) instead of
// the network.
#include "vault_remote.h"

RemoteKeysState g_preview_probe_state  = RemoteKeysState::Present;
int             g_preview_probe_status = -1;
bool            g_preview_probe_plain  = false;

RemoteKeys vault_fetch_remote_keys(const WebDavCfg&) {
    RemoteKeys r{g_preview_probe_state, {}, g_preview_probe_status};
    if (r.state == RemoteKeysState::Present) {
        r.bytes.assign(2, '{');
        r.plain = g_preview_probe_plain;
    }
    return r;
}

void vault_setup_wipe_recovery(VaultSetupResult* r) { r->recovery_hex.clear(); }
