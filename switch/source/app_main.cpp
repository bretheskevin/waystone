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
#include "saves.h"
#include "wsconfig.h"
#include "ui/session.h"
#include "ui/theme_tint.h"
#include "ui/setup_activity.h"
#include "ui/unlock_activity.h"

int main(int argc, char* argv[])
{
    socketInitializeDefault();
    romfsInit();
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

        const char* keys_path = "sdmc:/waystone/keys.json";
        FILE* kf = fopen(keys_path, "rb");
        if (kf) {
            fseek(kf, 0, SEEK_END);
            long klen = ftell(kf);
            fseek(kf, 0, SEEK_SET);
            uint8_t* kbuf = nullptr;
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
                brls::Application::pushActivity(
                    new UnlockActivity(&session, kbuf, static_cast<size_t>(klen)));
            } else {
                brls::Application::pushActivity(new SetupActivity(&session));
            }
            while (brls::Application::mainLoop()) {}
            free(kbuf);
        } else {
            brls::Application::pushActivity(new SetupActivity(&session));
            while (brls::Application::mainLoop()) {}
        }
    }

cleanup:
    if (session.vault) ws_vault_free(session.vault);
    session.vault = nullptr;
    curl_global_cleanup();
    romfsExit();
    socketExit();
    return 0;
}
