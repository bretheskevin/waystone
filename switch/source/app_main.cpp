#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <switch.h>
#include <curl/curl.h>
#include <borealis.hpp>

struct Vault;
extern "C" {
#include "waystone.h"
}

#include "net.h"
#include "net_status.h"
#include "saves.h"
#include "wsconfig.h"
#include "ui/session.h"
#include "ui/theme_tint.h"
#include "ui/setup_activity.h"
#include "ui/unlock_activity.h"
#include "ui/no_internet_activity.h"
#include "ui/loading_activity.h"
#include "session_store.h"

// Task 3: Switch hardware device key (SPL service — always available on Switch).
static bool switch_device_key(uint8_t* out_key, size_t* out_len) {
    if (!out_key || !out_len || *out_len < 8) return false;
    Result spl_rc = splInitialize();
    if (R_SUCCEEDED(spl_rc)) {
        uint64_t device_id = 0;
        Result rc = splGetConfig(SplConfigItem_DeviceId, &device_id);
        splExit();
        if (R_SUCCEEDED(rc)) {
            memcpy(out_key, &device_id, 8);
            *out_len = 8;
            return true;
        }
    }
    return false;
}

int main(int argc, char* argv[])
{
    socketInitializeDefault();
    romfsInit();
    session_store_set_device_key_fn(switch_device_key);  // Task 3: register before any session use
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        romfsExit();
        socketExit();
        return 1;
    }

    Session session;
    session.config_path = "sdmc:/waystone/config.json";
    session.config = wsconfig_load(session.config_path.c_str());

    if (!get_active_account(&session.uid))
        goto cleanup;
    session.device_id = get_device_id();
    if (session.device_id.empty())
        goto cleanup;

    apply_waystone_tint();

    if (brls::Application::init()) {
        brls::Application::createWindow("Waystone");
        brls::Application::setGlobalQuit(true);

        uint8_t* kbuf = nullptr;

        // Choose SetupActivity or UnlockActivity based on whether keys.json exists.
        // Task 6: when session.bin exists, try auto-unlock via LoadingActivity first.
        auto route_to_first_screen = [&]() {
            const char* keys_path = "sdmc:/waystone/keys.json";
            FILE* kf = fopen(keys_path, "rb");
            if (kf) {
                fseek(kf, 0, SEEK_END);
                long klen = ftell(kf);
                fseek(kf, 0, SEEK_SET);
                if (klen > 0) {
                    kbuf = static_cast<uint8_t*>(malloc(static_cast<size_t>(klen)));
                    if (kbuf) {
                        size_t got = fread(kbuf, 1, static_cast<size_t>(klen), kf);
                        if (got != static_cast<size_t>(klen)) {
                            free(kbuf);
                            kbuf = nullptr;
                            klen = 0;
                        }
                    }
                }
                fclose(kf);
                if (kbuf && klen > 0) {
                    if (session_store_exists()) {
                        auto worker = [&session]() -> LoadingActivity::LoadResult {
                            std::string webdav_pass;
                            WsVault* vault = session_store_load_vault(webdav_pass);
                            if (!vault) return {false, "session expired", {}};

                            session.vault          = vault;
                            session.dav.server_url = session.config.server_url;
                            session.dav.user       = session.config.username;
                            session.dav.pass       = std::move(webdav_pass);
                            zeroize_string(webdav_pass);

                            auto titles = list_titles();
                            return {true, "", std::move(titles)};
                        };

                        // on_failure runs from RefreshPump::run() on the render thread.
                        // pushActivity is safe; popActivity is NOT (UAF).
                        auto on_failure = [&session, kbuf, klen](const std::string&) {
                            session_store_clear();
                            brls::Application::pushActivity(
                                new UnlockActivity(&session, kbuf, static_cast<size_t>(klen)));
                        };

                        brls::Application::pushActivity(
                            new LoadingActivity(&session, worker, on_failure));
                    } else {
                        brls::Application::pushActivity(
                            new UnlockActivity(&session, kbuf, static_cast<size_t>(klen)));
                    }
                } else {
                    brls::Application::pushActivity(new SetupActivity(&session));
                }
            } else {
                brls::Application::pushActivity(new SetupActivity(&session));
            }
        };

        if (!network_available()) {
            brls::Application::pushActivity(
                new NoInternetActivity(NoInternetReason::NoNetwork,
                                       route_to_first_screen));
        } else {
            route_to_first_screen();
        }

        while (brls::Application::mainLoop()) {}
        free(kbuf);
    }

cleanup:
    if (session.vault) ws_vault_free(session.vault);
    session.vault = nullptr;
    curl_global_cleanup();
    romfsExit();
    socketExit();
    return 0;
}
