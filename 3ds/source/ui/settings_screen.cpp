#include "settings_screen.h"
#include "unlock_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include "swkbd_util.h"
#include "session_store.h"
#include "wsconfig.h"
#include "saves.h"
#include "keys_file.h"
#include "version.h"
#include "updater.h"
#include "keymap_3ds.h"
#include <cstdio>
#include <cstdlib>

struct Vault;
extern "C" {
#include "waystone.h"
}

SettingsScreen::SettingsScreen(Session* session)
    : session_(session),
      worker_(new UpdateWorker()),
      cursor_(0),
      phase_(UpdatePhase::Idle),
      last_phase_(UpdatePhase::Idle),
      confirm_update_(false),
      update_cancelled_(false),
      install_started_(false) {}

SettingsScreen::~SettingsScreen() {
    // Only join if we still own the worker (not detached by handle_input B).
    if (worker_) { worker_->request_cancel(); worker_->join(); delete worker_; }
}

void SettingsScreen::poll() {
    if (!worker_) return;
    phase_ = worker_->phase();

    // Live progress while phases run.
    if (phase_ == UpdatePhase::Checking) {
        status_text_ = "Checking for updates...";
    } else if (phase_ == UpdatePhase::Downloading) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Downloading... %d%%",
                 worker_->progress_percent());
        status_text_ = buf;
    }

    // ---- Finished a check ----
    if (last_phase_ == UpdatePhase::Checking &&
        (phase_ == UpdatePhase::Done || phase_ == UpdatePhase::Error)) {
        int rc = worker_->check_rc();
        if (rc == UP_OK) {
            std::string ver = worker_->latest_version();
            if (version_newer(ver.c_str(), WS_APP_VERSION)) {
                pending_ver_ = ver;
                pending_url_ = worker_->asset_url();
                confirm_update_ = true;
                status_text_ = "Update available: v" + ver;
                printf("[update] newer version available: v%s\n", ver.c_str());
            } else {
                status_text_ = "Up to date (v" + ver + ")";
            }
        } else {
            status_text_ = updater_check_message(rc, ".3dsx");
            printf("[update] check failed rc=%d\n", rc);
        }
    }

    // ---- Finished an install ----
    if (install_started_ && last_phase_ == UpdatePhase::Downloading &&
        (phase_ == UpdatePhase::Done || phase_ == UpdatePhase::Error)) {
        int rc = worker_->install_rc();
        if (rc == UP_OK) {
            status_text_ = "Updated to v" + pending_ver_ + ". Restart to apply.";
            printf("[update] install succeeded\n");
        } else {
            status_text_ = updater_install_message(rc);
            printf("[update] install failed rc=%d\n", rc);
        }
    }

    last_phase_ = phase_;
}

void SettingsScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    char header[64];
    snprintf(header, sizeof(header), "Waystone v%s", WS_APP_VERSION);
    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, header, (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 30.0f, 0.5f, TEXT_XL, CLR_TEXT, "Settings", (float)SCREEN_TOP_W);
    if (!status_text_.empty()) {
        u32 clr = CLR_NEUTRAL_400;
        if (phase_ == UpdatePhase::Error) clr = CLR_ERROR;
        else if (phase_ == UpdatePhase::Done && !confirm_update_ && !update_cancelled_)
            clr = CLR_SUCCESS;
        else if (phase_ == UpdatePhase::Checking ||
                 phase_ == UpdatePhase::Downloading) clr = CLR_SYNC;
        draw_text_centered(buf, 0, 80.0f, 0.5f, TEXT_BASE, clr, status_text_.c_str(), (float)SCREEN_TOP_W);
    }
}

void SettingsScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    float x = SP_MD;
    float w = (float)SCREEN_BOT_W - 2.0f * SP_MD;
    float row_h = 28.0f;
    float y = (float)SP_MD;

    // ---- Confirm banner replaces the row list while deciding ----
    if (confirm_update_) {
        char line[128];
        snprintf(line, sizeof(line), "v%s available", pending_ver_.c_str());
        static const WsAction update_acts[] = { WsAction::Update, WsAction::Cancel };
        std::string update_hint = ws_hint_bar(update_acts);
        draw_confirm_banner(buf, y, 150.0f, line, update_hint.c_str(), 0, 56.0f);
        draw_footer_hint(buf, update_hint.c_str());
        return;
    }

    const char* policy_str = (session_->config.conflict_policy == WsConflictPolicy::Prompt)
        ? "Prompt" : "Newest Wins";
    char row_labels[7][128];
    snprintf(row_labels[0], sizeof(row_labels[0]), "Server: %s", session_->config.server_url.c_str());
    snprintf(row_labels[1], sizeof(row_labels[1]), "Username: %s", session_->config.username.c_str());
    snprintf(row_labels[2], sizeof(row_labels[2]), "Conflict: %s", policy_str);
    snprintf(row_labels[3], sizeof(row_labels[3]), "Safety Backup: %s", session_->config.safety_backup ? "ON" : "OFF");
    snprintf(row_labels[4], sizeof(row_labels[4]), "Check for updates");
    snprintf(row_labels[5], sizeof(row_labels[5]), "Save");
    snprintf(row_labels[6], sizeof(row_labels[6]), "Log out");

    for (size_t i = 0; i < 4; i++) {
        bool focused = (cursor_ == i);
        u32 bg = focused ? CLR_PRIMARY_50 : CLR_CARD_BG;
        if (focused) {
            draw_rounded_rect(x - 1, y - 1, 0.49f, w + 2, row_h + 2, RAD_SM + 1, CLR_ACCENT);
        }
        draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, bg);
        draw_text(buf, x + SP_MD, y + SP_SM, 0.51f, TEXT_BASE, CLR_TEXT, row_labels[i]);
        y += row_h + SP_XS;
    }

    char dev_buf[128];
    snprintf(dev_buf, sizeof(dev_buf), "Device: %s", session_->device_id.c_str());
    draw_text(buf, x + SP_SM, y + 2.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT, dev_buf);
    y += 20.0f;

    for (size_t i = 4; i < NUM_ROWS; i++) {
        bool focused = (cursor_ == i);
        u32 bg = focused ? CLR_PRIMARY_50 : CLR_CARD_BG;
        u32 text_color = (i == 6) ? CLR_ERROR : CLR_TEXT;
        if (focused) {
            draw_rounded_rect(x - 1, y - 1, 0.49f, w + 2, row_h + 2, RAD_SM + 1, CLR_ACCENT);
        }
        draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, bg);
        draw_text(buf, x + SP_MD, y + SP_SM, 0.51f, TEXT_BASE, text_color, row_labels[i]);
        y += row_h + SP_XS;
    }

    static const WsAction row_acts[] = { WsAction::Select, WsAction::Back };
    draw_footer_hint(buf, (ws_hint_bar(row_acts) + "  DPad: Nav").c_str());
}

void SettingsScreen::handle_input(u32 kDown, touchPosition touch) {
    (void)touch;

    // ---- Confirm mode (update now?) ----
    if (confirm_update_) {
        if (kDown & ws_key(WsAction::Update)) {
            printf("[update] confirmed: installing v%s\n", pending_ver_.c_str());
            confirm_update_ = false;
            update_cancelled_ = false;
            install_started_ = true;
            status_text_ = "Downloading...";
            last_phase_ = UpdatePhase::Idle;  // re-arm transition detection
            worker_->start_install(pending_url_);
        } else if (kDown & ws_key(WsAction::Cancel)) {
            printf("[update] update cancelled\n");
            confirm_update_ = false;
            update_cancelled_ = true;  // keep "Update cancelled" out of the success color
            status_text_ = "Update cancelled";
        }
        return;
    }

    if (kDown & KEY_DUP)   { cursor_ = (cursor_ == 0) ? NUM_ROWS - 1 : cursor_ - 1; }
    if (kDown & KEY_DDOWN) { cursor_ = (cursor_ + 1) % NUM_ROWS; }
    if (kDown & ws_key(WsAction::Back)) {
        printf("[ui] settings back\n");
        reap_worker(worker_);  // instant pop: dtor never joins a live worker
        App::instance().pop_screen();
        return;
    }
    if (kDown & ws_key(WsAction::Select)) {
        switch (cursor_) {
            case 0: {
                std::string v = swkbd_prompt("Server URL", false, session_->config.server_url);
                if (!v.empty()) {
                    session_->config.server_url = v;
                    session_->dav.server_url = v;
                    zeroize_string(session_->dav.pass);
                }
                break;
            }
            case 1: {
                std::string v = swkbd_prompt("Username", false, session_->config.username);
                if (!v.empty()) {
                    session_->config.username = v;
                    session_->dav.user = v;
                }
                break;
            }
            case 2: {
                session_->config.conflict_policy =
                    (session_->config.conflict_policy == WsConflictPolicy::NewestWins)
                        ? WsConflictPolicy::Prompt : WsConflictPolicy::NewestWins;
                break;
            }
            case 3: {
                session_->config.safety_backup = !session_->config.safety_backup;
                break;
            }
            case 4: {
                printf("[update] check requested from settings\n");
                install_started_ = false;
                confirm_update_ = false;
                update_cancelled_ = false;
                pending_ver_.clear();
                pending_url_.clear();
                status_text_ = "Checking for updates...";
                last_phase_ = UpdatePhase::Idle;  // re-arm transition detection
                phase_ = UpdatePhase::Checking;
                worker_->start_check();
                break;
            }
            case 5: {
                printf("[ui] settings save -> %s\n", session_->config_path.c_str());
                if (wsconfig_save(session_->config, session_->config_path.c_str())) {
                    printf("[ui] settings save ok\n");
                    status_text_ = "Settings saved";
                } else {
                    printf("[ui] settings save FAILED\n");
                    status_text_ = "Failed to save settings";
                }
                break;
            }
            case 6: {
                printf("[ui] settings: log out\n");
                // set_screen/pop_screen below delete this screen; detach first so
                // ~SettingsScreen never joins a running worker on the render thread.
                reap_worker(worker_);
                session_store_clear();
                if (session_->vault) { ws_vault_free(session_->vault); session_->vault = 0; }
                zeroize_string(session_->dav.pass);
                zeroize_string(session_->dav.user);
                long klen = 0;
                uint8_t* kbuf = read_keys_file("sdmc:/waystone/keys.json", &klen);
                if (kbuf && klen > 0) {
                    // kbuf intentionally not freed: UnlockScreen borrows it for app lifetime
                    App::instance().set_screen(new UnlockScreen(session_, kbuf, static_cast<size_t>(klen)));
                    return;
                }
                App::instance().pop_screen();
                return;
            }
        }
    }
}
