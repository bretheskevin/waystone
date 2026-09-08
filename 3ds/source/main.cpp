#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <3ds.h>
#include <sys/select.h>
#include <curl/curl.h>

struct Vault;
extern "C" {
#include "waystone.h"
}

#include "net.h"
#include "saves.h"
#include "sync.h"

#ifndef WAYSTONE_WEBDAV_URL
#define WAYSTONE_WEBDAV_URL "http://CHANGEME"
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
    u32* SOC_buffer = static_cast<u32*>(memalign(0x1000, 0x100000));
    WsBuf recovery  = {nullptr, 0};
    WsBuf keys      = {nullptr, 0};
    WsVault* vault  = nullptr;
    bool ps_ok      = false;
    bool soc_ok     = false;
    bool romfs_ok   = false;
    bool curl_ok    = false;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("=== Waystone 3DS Save Engine ===\n\n");

    if (!SOC_buffer) {
        printf("FATAL: SOC buffer alloc failed\n");
        goto cleanup;
    }

    if (psInit() != 0) {
        printf("FATAL: psInit failed\n");
        goto cleanup;
    }
    ps_ok = true;

    if (socInit(SOC_buffer, 0x100000) != 0) {
        printf("FATAL: socInit failed\n");
        goto cleanup;
    }
    soc_ok = true;

    if (romfsInit() != 0) {
        printf("FATAL: romfsInit failed\n");
        goto cleanup;
    }
    romfs_ok = true;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        printf("FATAL: curl_global_init failed\n");
        goto cleanup;
    }
    curl_ok = true;

    // -- Vault init --
    {
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
                    if (got == static_cast<size_t>(klen))
                        vault = ws_vault_unlock_pass(WAYSTONE_VAULT_PASS, kbuf, got);
                    free(kbuf);
                }
            }
            fclose(kf);
        }
    }

    if (!vault) {
        printf("No existing vault found, creating new...\n");
        vault = ws_vault_init(WAYSTONE_VAULT_PASS, &recovery, &keys);
        if (!vault) {
            printf("FATAL: ws_vault_init failed: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            goto cleanup;
        }
        mkdir("sdmc:/waystone", 0755);
        FILE* wf = fopen("sdmc:/waystone/keys.json", "wb");
        if (wf) {
            fwrite(keys.ptr, 1, keys.len, wf);
            fclose(wf);
            printf("Keys saved to sdmc:/waystone/keys.json\n");
        }
        if (recovery.ptr)
            printf("RECOVERY KEY (save this!): %.*s\n",
                   static_cast<int>(recovery.len), recovery.ptr);
    } else {
        printf("Vault unlocked from existing keys.\n");
    }

    // -- Device ID + sync --
    {
        std::string device_id = get_device_id();
        if (device_id.empty()) {
            printf("FATAL: could not get device ID\n");
            goto cleanup;
        }
        printf("Device ID: %s\n\n", device_id.c_str());

        WebDavCfg dav = {WAYSTONE_WEBDAV_URL, WAYSTONE_WEBDAV_USER, WAYSTONE_WEBDAV_PASS};

        printf("--- Enumerating titles ---\n");
        std::vector<TitleInfo> titles = list_titles();
        printf("Found %zu titles\n\n", titles.size());

        printf("--- Push phase ---\n");
        int total_pushed = 0;
        for (size_t i = 0; i < titles.size(); i++) {
            printf("[%zu/%zu] %s (TID %016llX)\n",
                   i + 1, titles.size(),
                   titles[i].name.c_str(),
                   static_cast<unsigned long long>(titles[i].title_id));
            int rc = push_title(vault, titles[i], device_id.c_str(), dav);
            if (rc > 0) total_pushed += rc;
        }
        printf("\n=== Push done: %d saves pushed ===\n\n", total_pushed);

        printf("--- Pull phase ---\n");
        int total_pulled = 0;
        for (size_t i = 0; i < titles.size(); i++) {
            printf("[%zu/%zu] %s (TID %016llX)\n",
                   i + 1, titles.size(),
                   titles[i].name.c_str(),
                   static_cast<unsigned long long>(titles[i].title_id));
            int rc = pull_title(vault, titles[i], device_id.c_str(), dav);
            if (rc > 0) total_pulled += rc;
        }
        printf("\n=== Pull done: %d saves pulled ===\n", total_pulled);
    }

cleanup:
    ws_buf_free(recovery);
    ws_buf_free(keys);
    if (vault) ws_vault_free(vault);
    if (curl_ok) curl_global_cleanup();
    if (romfs_ok) romfsExit();
    if (soc_ok) socExit();
    if (ps_ok) psExit();
    free(SOC_buffer);

    printf("\nPress START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
