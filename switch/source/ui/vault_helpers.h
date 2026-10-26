#pragma once
#include "session.h"
#include "keys_file.h"
#include "vault_remote.h"
#include <string>
#include <vector>

// Worker thread (blocks: network + Argon2). Creates the vault and PUTs it to the server
// (refused when one exists), then writes keys.json, the recovery file and config.json.
// session->config.server_url / username must be set before calling.
VaultSetupResult setup_create_vault(Session* session, const WebDavCfg& dav,
                                    const std::string& passphrase);

// Worker thread (blocks: Argon2). Unlocks the server's keys.json with the passphrase or
// recovery key, then writes keys.json + config.json locally.
VaultSetupResult setup_join_vault(Session* session, const std::vector<uint8_t>& server_keys,
                                  const std::string& secret, bool recovery);

// Worker thread, after every successful unlock: uploads a local-only vault's keys.json when
// the server has none (failures only logged), then lists titles.
std::vector<TitleInfo> load_titles_after_unlock(Session* session);

// After vault creation or unlock: build SyncController and push TitleListActivity.
// session must have vault, uid, device_id, dav fully populated.
void push_dashboard(Session* session);
