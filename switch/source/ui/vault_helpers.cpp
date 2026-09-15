#include "vault_helpers.h"
#include "loading_activity.h"
#include "sync_controller.h"
#include "title_list_activity.h"
#include <borealis.hpp>
#include <cstdio>
#include <sys/stat.h>

extern "C" {
struct Vault;
#include "waystone.h"
}

VaultCreateResult create_vault(const std::string& passphrase, Session* session) {
    VaultCreateResult result = {nullptr, "", "", ""};

    const char* keys_path = "sdmc:/waystone/keys.json";
    FILE* existing = fopen(keys_path, "rb");
    if (existing) {
        fseek(existing, 0, SEEK_END);
        long esz = ftell(existing);
        fclose(existing);
        if (esz > 0) {
            result.error = "keys.json already exists! Aborting setup.";
            return result;
        }
    }

    WsBuf recovery = {nullptr, 0};
    WsBuf keys = {nullptr, 0};
    WsVault* vault = ws_vault_init(passphrase.c_str(), &recovery, &keys);

    if (!vault) {
        const char* err = ws_last_error();
        result.error = std::string("Vault creation failed: ") + (err ? err : "unknown");
        ws_buf_free(recovery);
        ws_buf_free(keys);
        return result;
    }

    mkdir("sdmc:/waystone", 0755);
    FILE* wf = fopen(keys_path, "wb");
    if (wf) {
        fwrite(keys.ptr, 1, keys.len, wf);
        fclose(wf);
    }
    ws_buf_free(keys);

    wsconfig_save(session->config, session->config_path.c_str());

    result.vault = vault;

    if (recovery.ptr && recovery.len > 0) {
        result.recovery_hex = std::string(reinterpret_cast<const char*>(recovery.ptr), recovery.len);
        result.recovery_path = "sdmc:/waystone/recovery-" + session->device_id + ".txt";
        FILE* rf = fopen(result.recovery_path.c_str(), "wb");
        if (rf) {
            fwrite(recovery.ptr, 1, recovery.len, rf);
            fclose(rf);
        }
    }
    ws_buf_free(recovery);

    return result;
}

// Task 5: push LoadingActivity; the worker runs list_titles() off the render thread.
void push_dashboard(Session* session) {
    auto worker = [session]() -> LoadingActivity::LoadResult {
        auto titles = list_titles();
        return {true, "", std::move(titles)};
    };

    brls::Application::pushActivity(new LoadingActivity(session, worker));
}
