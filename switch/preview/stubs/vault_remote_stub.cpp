// Preview stub for shell-common/vault_remote.cpp: the setup probe answers from a fixture the
// mode sets (g_preview_probe_state / g_preview_probe_status / g_preview_probe_plain) instead of
// the network.
#include "vault_remote.h"
#include "session_store.h"
#include <cstdio>

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

bool vault_forget_local(const char* keys_path, const std::string& recovery_path, WsVault*& vault,
                        std::string& dav_user, std::string& dav_pass) {
    printf("[vault] preview stub: forget local keys=%s recovery=%s (nothing deleted)\n",
           keys_path ? keys_path : "(null)", recovery_path.c_str());
    session_drop_login(vault, dav_user, dav_pass);
    return true;
}
