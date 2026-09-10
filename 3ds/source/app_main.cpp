#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <3ds.h>
#include <sys/select.h>
#include <citro3d.h>
#include <citro2d.h>
#include <curl/curl.h>

struct Vault;
extern "C" {
#include "waystone.h"
}

#include "saves.h"
#include "wsconfig.h"
#include "ui/app.h"
#include "ui/session.h"
#include "ui/setup_screen.h"
#include "ui/unlock_screen.h"

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    u32* SOC_buffer = static_cast<u32*>(memalign(0x1000, 0x100000));
    bool ps_ok=false, soc_ok=false, romfs_ok=false, curl_ok=false;
    uint8_t* kbuf = 0;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    if (!SOC_buffer) { printf("FATAL: SOC buffer alloc failed\n"); goto cleanup; }
    if (psInit() != 0) { printf("FATAL: psInit failed\n"); goto cleanup; }
    ps_ok = true;
    if (socInit(SOC_buffer, 0x100000) != 0) { printf("FATAL: socInit failed\n"); goto cleanup; }
    soc_ok = true;
    if (romfsInit() != 0) { printf("FATAL: romfsInit failed\n"); goto cleanup; }
    romfs_ok = true;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) { printf("FATAL: curl_global_init failed\n"); goto cleanup; }
    curl_ok = true;

    {
        Session session;
        session.config_path = "sdmc:/waystone/config.json";
        session.config = wsconfig_load(session.config_path.c_str());
        session.device_id = get_device_id();
        if (session.device_id.empty()) { printf("FATAL: could not get device ID\n"); goto cleanup; }

        {
            App app;

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
                        if (got != static_cast<size_t>(klen)) { free(kbuf); kbuf = 0; klen = 0; }
                    }
                }
                fclose(kf);
                if (kbuf && klen > 0) {
                    app.set_screen(new UnlockScreen(&session, kbuf, static_cast<size_t>(klen)));
                } else {
                    app.set_screen(new SetupScreen(&session));
                }
            } else {
                app.set_screen(new SetupScreen(&session));
            }

            app.run();
        } // ~App: all screens deleted, worker threads joined before vault is freed

        if (session.vault) { ws_vault_free(session.vault); session.vault = 0; }
    }

cleanup:
    free(kbuf);
    if (curl_ok) curl_global_cleanup();
    if (romfs_ok) romfsExit();
    if (soc_ok) socExit();
    if (ps_ok) psExit();
    free(SOC_buffer);

    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
