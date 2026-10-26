#include "vault_helpers.h"
#include "loading_activity.h"
#include "sync_controller.h"
#include "title_list_activity.h"
#include <borealis.hpp>
#include <cstdio>

static void persist_setup(const Session* session, const VaultSetupResult& r,
                          const std::string& recovery_path) {
    vault_setup_persist(r, WS_KEYS_PATH, recovery_path, session->config,
                        session->config_path.c_str());
}

VaultSetupResult setup_create_vault(Session* session, const WebDavCfg& dav,
                                    const std::string& passphrase) {
    VaultSetupResult r = vault_create_and_publish(dav, passphrase);
    if (r.vault) persist_setup(session, r, ws_recovery_path(session->device_id));
    printf("[vault] setup create: %s\n", r.vault ? "ok" : r.error.c_str());
    return r;
}

VaultSetupResult setup_join_vault(Session* session, const std::vector<uint8_t>& server_keys,
                                  const std::string& secret, bool recovery) {
    VaultSetupResult r = vault_join(server_keys, secret, recovery);
    if (r.vault) persist_setup(session, r, "");
    printf("[vault] setup join: %s\n", r.vault ? "ok" : r.error.c_str());
    return r;
}

std::vector<TitleInfo> load_titles_after_unlock(Session* session) {
    vault_publish_local_if_absent(session->dav.as_cfg(), WS_KEYS_PATH);
    return list_titles(session->vault, session->dav.as_cfg(), session->uid);
}

void push_dashboard(Session* session) {
    auto worker = [session]() -> LoadingActivity::LoadResult {
        return {true, "", load_titles_after_unlock(session)};
    };

    brls::Application::pushActivity(new LoadingActivity(session, worker));
}
