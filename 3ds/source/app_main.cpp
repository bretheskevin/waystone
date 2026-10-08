#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <3ds.h>
#include <3ds/services/cfgu.h>
#include <sys/select.h>
#include <citro3d.h>
#include <citro2d.h>
#include <curl/curl.h>

struct Vault;
extern "C" {
#include "waystone.h"
}

#include "saves.h"
#include "keys_file.h"
#include "wsconfig.h"
#include "session_store.h"
#include "secure_clear.h"
#include "updater.h"
#include "ui/app.h"
#include "ui/session.h"
#include "ui/setup_screen.h"
#include "ui/unlock_screen.h"
#include "ui/loading_screen.h"
#include "ui/worker_thread.h"
#include "net_status.h"
#include "ui/no_internet_screen.h"

static bool ctr_device_key(uint8_t* out_key, size_t* out_len) {
    if (!out_key || !out_len || *out_len < 8) return false;
    Result rc = CFGU_GetConfigInfoBlk2(8, 0x00090001, out_key);
    if (R_FAILED(rc)) return false;
    *out_len = 8;
    return true;
}

static const char* const LOG_DIR = "sdmc:/waystone";
static const char* const LOG_PATH = "sdmc:/waystone/log.txt";

static void redirect_logs() {
    int link_fd = link3dsStdio();
    if (link_fd >= 0) {
        printf("[net] link3dsStdio fd=%d\n", link_fd);
        return;
    }

    mkdir(LOG_DIR, 0755);
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("[sys] log file open failed path=%s errno=%d (link3dsStdio fd=%d)\n",
               LOG_PATH, errno, link_fd);
        return;
    }
    fflush(stdout);
    fflush(stderr);
    // Both streams share one handle (and so one file offset) so interleaved writes append.
    int out_rc = dup2(fd, STDOUT_FILENO);
    int err_rc = dup2(fd, STDERR_FILENO);
    close(fd);
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    setvbuf(stderr, NULL, _IOLBF, BUFSIZ);
    printf("[sys] logging to %s (link3dsStdio fd=%d dup2 out=%d err=%d)\n",
           LOG_PATH, link_fd, out_rc, err_rc);
}

int main(int argc, char* argv[]) {
    // Plain setter, safe before any init: records where we were launched from so
    // the self-updater can find (and replace) the running .3dsx later.
    updater_set_self_candidates(argc > 0 && argv ? argv[0] : 0,
                            "sdmc:/3ds/waystone/waystone-3ds-spike.3dsx");
    u32* SOC_buffer = static_cast<u32*>(memalign(0x1000, 0x100000));
    bool ps_ok=false, cfgu_ok=false, soc_ok=false, romfs_ok=false, curl_ok=false;
    uint8_t* kbuf = 0;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    if (!SOC_buffer) { printf("FATAL: SOC buffer alloc failed\n"); goto cleanup; }
    if (psInit() != 0) { printf("FATAL: psInit failed\n"); goto cleanup; }
    ps_ok = true;
    if (cfguInit() != 0) { printf("FATAL: cfguInit failed\n"); goto cleanup; }
    cfgu_ok = true;
    session_store_set_device_key_fn(ctr_device_key);
    if (socInit(SOC_buffer, 0x100000) != 0) { printf("FATAL: socInit failed\n"); goto cleanup; }
    soc_ok = true;
    redirect_logs();

    if (is_new_3ds()) {
        osSetSpeedupEnable(true);
        printf("[sys] model=New3DS speedup=on\n");
    } else {
        printf("[sys] model=Old3DS speedup=n/a\n");
    }

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

            long klen = 0;
            kbuf = read_keys_file("sdmc:/waystone/keys.json", &klen);

            // Wrap routing in a lambda so the no-internet gate can retry it.
            auto route_to_first_screen = [&]() {
                if (kbuf && klen > 0 && session_store_exists()) {
                    app.set_screen(new LoadingScreen(&session, true, kbuf, static_cast<size_t>(klen)));
                } else if (kbuf && klen > 0) {
                    app.set_screen(new UnlockScreen(&session, kbuf, static_cast<size_t>(klen)));
                } else {
                    free(kbuf); kbuf = 0;
                    app.set_screen(new SetupScreen(&session));
                }
            };

            if (!network_available()) {
                app.set_screen(new NoInternetScreen(NoInternetReason::NoNetwork,
                                                    route_to_first_screen));
            } else {
                route_to_first_screen();
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
    if (cfgu_ok) cfguExit();
    if (ps_ok) psExit();
    free(SOC_buffer);

    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
