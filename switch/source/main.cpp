#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <switch.h>
#include <curl/curl.h>

struct Vault;
extern "C" {
#include "waystone.h"
}

#include "net.h"
#include "saves.h"
#include "sync.h"

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

int main(int argc, char* argv[]) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("=== Waystone Switch Save Engine ===\n\n");

    // -- Vault init --
    WsBuf recovery = {nullptr, 0};
    WsBuf keys     = {nullptr, 0};
    WsVault* vault = nullptr;

    // Try to load existing keys from SD card
    const char* keys_path = "sdmc:/waystone/keys.json";
    FILE* kf = fopen(keys_path, "rb");
    if (kf) {
        fseek(kf, 0, SEEK_END);
        long klen = ftell(kf);
        fseek(kf, 0, SEEK_SET);
        if (klen > 0) {
            uint8_t* kbuf = static_cast<uint8_t*>(malloc(static_cast<size_t>(klen)));
            if (kbuf) {
                size_t got = fread(kbuf, 1, static_cast<size_t>(klen), kf);
                if (got == static_cast<size_t>(klen)) {
                    vault = ws_vault_unlock_pass(WAYSTONE_VAULT_PASS, kbuf, got);
                }
                free(kbuf);
            }
        }
        fclose(kf);
    }

    if (!vault) {
        printf("No existing vault found, creating new...\n");
        vault = ws_vault_init(WAYSTONE_VAULT_PASS, &recovery, &keys);
        if (!vault) {
            printf("FATAL: ws_vault_init failed: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            goto cleanup;
        }
        // Persist keys to SD card
        mkdir("sdmc:/waystone", 0755);
        FILE* wf = fopen(keys_path, "wb");
        if (wf) {
            fwrite(keys.ptr, 1, keys.len, wf);
            fclose(wf);
            printf("Keys saved to %s\n", keys_path);
        }
        if (recovery.ptr) {
            printf("RECOVERY KEY (save this!): %.*s\n",
                   static_cast<int>(recovery.len), recovery.ptr);
        }
    } else {
        printf("Vault unlocked from existing keys.\n");
    }

    // -- Account --
    {
        AccountUid uid = {};
        if (!get_active_account(&uid)) {
            printf("FATAL: no account\n");
            goto cleanup;
        }
        printf("Account UID: %016lX%016lX\n", uid.uid[0], uid.uid[1]);

        // -- Device ID --
        {
            std::string device_id = get_device_id();
            if (device_id.empty()) {
                printf("FATAL: could not get device ID\n");
                goto cleanup;
            }
            printf("Device ID: %s\n\n", device_id.c_str());

            // -- Network --
            socketInitializeDefault();
            romfsInit();
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
                printf("FATAL: curl_global_init failed\n");
                romfsExit();
                socketExit();
                goto cleanup;
            }

            WebDavCfg dav = {
                WAYSTONE_WEBDAV_URL,
                WAYSTONE_WEBDAV_USER,
                WAYSTONE_WEBDAV_PASS
            };

            // -- List titles --
            printf("--- Enumerating titles ---\n");
            std::vector<TitleInfo> titles = list_titles();
            printf("Found %zu titles\n\n", titles.size());

            // -- Sync run: one WebDAV session wraps push + pull phases --
            WebDavSession* sess = webdav_session_begin(dav);
            if (!sess) {
                printf("FATAL: webdav_session_begin failed\n");
            } else {
                // -- Push each title --
                printf("--- Push phase ---\n");
                int total_pushed = 0;
                for (size_t i = 0; i < titles.size(); i++) {
                    printf("[%zu/%zu] %s (TID %016lX)\n",
                           i + 1, titles.size(),
                           titles[i].name.c_str(), titles[i].title_id);
                    int rc = push_title(vault, titles[i], uid,
                                        device_id.c_str(), sess);
                    if (rc > 0) total_pushed += rc;
                }
                printf("\n=== Push done: %d saves pushed ===\n", total_pushed);

                // -- Pull first title (restore demo) --
                // Pulls the first title only to keep this a thin console driver.
                // A future UI pass will iterate all titles like push does.
                printf("\n--- Pull phase (first title) ---\n");
                if (!titles.empty()) {
                    printf("[1/%zu] %s (TID %016lX)\n",
                           titles.size(),
                           titles[0].name.c_str(), titles[0].title_id);
                    int prc = pull_title(vault, titles[0], uid,
                                         device_id.c_str(), sess);
                    if (prc >= 0)
                        printf("Pull result: %d save(s) restored.\n", prc);
                    else
                        printf("Pull failed (see messages above).\n");
                } else {
                    printf("No titles found — nothing to pull.\n");
                }
                webdav_session_end(sess);
            }

            curl_global_cleanup();
            romfsExit();
            socketExit();
        }
    }

cleanup:
    ws_buf_free(recovery);
    ws_buf_free(keys);
    if (vault) ws_vault_free(vault);

    printf("\nPress + to exit.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
