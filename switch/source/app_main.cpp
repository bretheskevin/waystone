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
#include "ui/sync_controller.h"
#include "ui/title_list_activity.h"

#ifndef WAYSTONE_WEBDAV_URL
#define WAYSTONE_WEBDAV_URL "https://CHANGEME"
#endif
#ifndef WAYSTONE_WEBDAV_USER
#define WAYSTONE_WEBDAV_USER "changeme"
#endif
#ifndef WAYSTONE_WEBDAV_PASS
#define WAYSTONE_WEBDAV_PASS "changeme"
#endif
#ifndef WAYSTONE_VAULT_PASS
#define WAYSTONE_VAULT_PASS "changeme"
#endif

int main(int argc, char* argv[])
{
    socketInitializeDefault();
    romfsInit();
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        goto cleanup;

    WsBuf recovery = {nullptr, 0};
    WsBuf keys     = {nullptr, 0};
    WsVault* vault = nullptr;

    const char* keys_path = "sdmc:/waystone/keys.json";
    FILE* kf = fopen(keys_path, "rb");
    if (kf)
    {
        fseek(kf, 0, SEEK_END);
        long klen = ftell(kf);
        fseek(kf, 0, SEEK_SET);
        if (klen > 0)
        {
            uint8_t* kbuf = static_cast<uint8_t*>(malloc(static_cast<size_t>(klen)));
            if (kbuf)
            {
                size_t got = fread(kbuf, 1, static_cast<size_t>(klen), kf);
                if (got == static_cast<size_t>(klen))
                    vault = ws_vault_unlock_pass(WAYSTONE_VAULT_PASS, kbuf, got);
                free(kbuf);
            }
        }
        fclose(kf);
    }

    if (!vault)
    {
        vault = ws_vault_init(WAYSTONE_VAULT_PASS, &recovery, &keys);
        if (!vault)
            goto cleanup;
        mkdir("sdmc:/waystone", 0755);
        FILE* wf = fopen(keys_path, "wb");
        if (wf)
        {
            fwrite(keys.ptr, 1, keys.len, wf);
            fclose(wf);
        }
    }

    {
        AccountUid uid = {};
        if (!get_active_account(&uid))
            goto cleanup;

        std::string device_id = get_device_id();
        if (device_id.empty())
            goto cleanup;

        WebDavCfg dav = {
            WAYSTONE_WEBDAV_URL,
            WAYSTONE_WEBDAV_USER,
            WAYSTONE_WEBDAV_PASS
        };

        std::vector<TitleInfo> titles = list_titles();
        SyncController ctrl(vault, uid, device_id, dav, std::move(titles));

        if (brls::Application::init())
        {
            brls::Application::createWindow("Waystone");
            brls::Application::setGlobalQuit(true);
            brls::Application::pushActivity(new TitleListActivity(&ctrl));
            while (brls::Application::mainLoop()) {}
        }

        ctrl.join();
    }

cleanup:
    ws_buf_free(recovery);
    ws_buf_free(keys);
    if (vault)
        ws_vault_free(vault);
    curl_global_cleanup();
    romfsExit();
    socketExit();

    return 0;
}
