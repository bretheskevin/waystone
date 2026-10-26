#include "settings_pane.h"
#include "swkbd_util.h"
#include "session_store.h"
#include "keymap_switch.h"
#include "keys_file.h"
#include "unlock_activity.h"
#include "worker_reaper.h"
#include "pane_menu.h"
#include "version.h"
#include "updater.h"
#include "kit/confirm_dialog.h"
#include "kit/overlay.h"
#include "kit/toast.h"
#include <switch.h>
#include <cstdio>
#include <functional>

extern "C" {
struct Vault;
#include "waystone.h"
}

static constexpr float LIST_MARGIN_TOP = 16.0f;

static const char* policy_name(WsConflictPolicy p) {
    return p == WsConflictPolicy::Prompt ? "Prompt" : "Newest Wins";
}

SettingsPane::SettingsPane(Session* session)
    : brls::Box(brls::Axis::COLUMN), session_(session), updater_(new UpdateController()) {
    ws_setup_pane(this);
    setPaddingTop(LIST_MARGIN_TOP);
    list_ = ws_add_pane_menu(this);

    server_row_   = list_->addRow("Server", "", [this] { edit_server(); });
    user_row_     = list_->addRow("WebDAV user", "", [this] { edit_user(); });
    password_row_ = list_->addRow("WebDAV password", "", [this] { edit_password(); });
    policy_row_   = list_->addRow("Conflict policy", "", [this] { toggle_policy(); });
    backup_row_   = list_->addRow("Safety backup", "", [this] { toggle_backup(); });
    device_row_   = list_->addRow("Device");
    device_row_->setFocusable(false);
    update_row_   = list_->addRow("Check for updates", "", [this] { check_updates(); });
    list_->addRow("Version", std::string("v") + WS_APP_VERSION)->setFocusable(false);
    list_->addRow("Save", "", [this] { save_settings(); });
    list_->addRow("Log out", "", [this] { request_logout(); });

    pump_.start();
    refresh_labels();
    printf("[ui] settings pane: created\n");
}

SettingsPane::~SettingsPane() {
    pump_.stop();
    reap_worker(updater_);
    printf("[ui] settings pane: destroyed\n");
}

std::string SettingsPane::guide() const {
    return ws_hint_bar({WsAction::Select, WsAction::Back, WsAction::Quit});
}

void SettingsPane::onShown() {
    printf("[ui] settings pane: shown\n");
    refresh_labels();
}

void SettingsPane::refresh_labels() {
    server_row_->setDetail(session_->config.server_url);
    user_row_->setDetail(session_->config.username);
    password_row_->setDetail(webdav_pass_mask(session_->dav.pass));
    policy_row_->setDetail(policy_name(session_->config.conflict_policy));
    backup_row_->setDetail(session_->config.safety_backup ? "ON" : "OFF");
    device_row_->setDetail(session_->device_id);
}

void SettingsPane::set_update_status(const std::string& text) { update_row_->setDetail(text); }

void SettingsPane::edit_server() {
    if (refuse_while_busy("server edit")) return;
    std::string val = swkbd_prompt("Server URL", session_->config.server_url, false);
    if (val.empty()) return;
    printf("[ui] settings: server url changed\n");
    session_->config.server_url = val;
    session_->dav.server_url = val;
    zeroize_string(session_->dav.pass);
    refresh_labels();
}

void SettingsPane::edit_user() {
    if (refuse_while_busy("username edit")) return;
    std::string val = swkbd_prompt("Username", session_->config.username, false);
    if (val.empty()) return;
    printf("[ui] settings: username changed\n");
    session_->config.username = val;
    session_->dav.user = val;
    refresh_labels();
}

void SettingsPane::edit_password() {
    if (refuse_while_busy("password edit")) return;
    std::string val = swkbd_prompt("WebDAV Password", "", true);
    if (update_webdav_pass(session_->vault, session_->dav.pass, val)) {
        ws_toast("WebDAV password updated");
        refresh_labels();
    }
}

void SettingsPane::toggle_policy() {
    session_->config.conflict_policy =
        (session_->config.conflict_policy == WsConflictPolicy::NewestWins)
            ? WsConflictPolicy::Prompt
            : WsConflictPolicy::NewestWins;
    printf("[ui] settings: conflict policy -> %s\n", policy_name(session_->config.conflict_policy));
    refresh_labels();
}

void SettingsPane::toggle_backup() {
    session_->config.safety_backup = !session_->config.safety_backup;
    printf("[ui] settings: safety backup -> %s\n", session_->config.safety_backup ? "ON" : "OFF");
    refresh_labels();
}

void SettingsPane::check_updates() {
    if (updater_->is_running()) return;
    printf("[update] check requested from settings\n");
    install_started_ = false;
    updater_->start_check();
    last_phase_ = UpdatePhase::Checking;  // a check that fails before the next poll still reports
    set_update_status("Checking\xe2\x80\xa6");
}

void SettingsPane::save_settings() {
    printf("[ui] settings save -> %s\n", session_->config_path.c_str());
    if (wsconfig_save(session_->config, session_->config_path.c_str())) {
        printf("[ui] settings save ok\n");
        ws_toast("Settings saved");
    } else {
        printf("[ui] settings save FAILED\n");
        ws_toast("Failed to save settings");
    }
}

bool SettingsPane::refuse_while_busy(const char* what) const {
    return refuse_if_busy(BusyScope::Credentials, session_, "[ui] settings:", what);
}

void SettingsPane::request_logout() {
    if (refuse_while_busy("log out")) return;
    if (logout_pending_) return;
    printf("[ui] settings: log out requested (deferred one frame)\n");
    logout_pending_ = true;
    ws_toast("Logging out\xe2\x80\xa6");
    pump_.schedule();
}

void SettingsPane::do_logout() {
    if (!logout_pending_) return;
    logout_pending_ = false;
    printf("[vault] logout: clearing session + freeing vault\n");
    session_store_clear();
    if (session_->vault) {
        ws_vault_free(session_->vault);
        session_->vault = nullptr;
    }
    zeroize_string(session_->dav.pass);  // in place: OwnedWebDavCfg is non-movable
    zeroize_string(session_->dav.user);
    long klen = 0;
    uint8_t* kbuf = read_keys_file(WS_KEYS_PATH, &klen);
    if (!kbuf || klen <= 0) {
        printf("[vault] logout: keys.json unreadable -- staying on Settings\n");
        ws_toast("Logged out. Please restart the app.");
        return;
    }
    // kbuf intentionally never freed: UnlockActivity borrows it for the app lifetime (3DS does the same).
    printf("[ui] logout -> push UnlockActivity (activities below stay; not popped)\n");
    brls::Application::pushActivity(new UnlockActivity(session_, kbuf, static_cast<size_t>(klen)));
}

void SettingsPane::offer_install() {
    dialog_open_ = true;
    printf("[update] offering install of v%s\n", pending_ver_.c_str());
    OverlayActivity::push(new ConfirmDialog(
        "Update available", "Waystone " + pending_ver_ + " is ready to install.",
        ws_if_alive(alive_, "update install", [this] {
            dialog_open_ = false;
            start_install();
        }),
        ws_if_alive(alive_, "update postpone", [this] {
            dialog_open_ = false;
            printf("[update] confirm declined (back)\n");
            set_update_status("Update postponed");
        }),
        WsAction::Update));
}

void SettingsPane::offer_restart() {
    dialog_open_ = true;
    printf("[update] offering restart into v%s\n", pending_ver_.c_str());
    OverlayActivity::push(new ConfirmDialog(
        "Update installed", "Restart Waystone now?",
        ws_if_alive(alive_, "update restart", [this] {
            dialog_open_ = false;
            restart_now();
        }),
        ws_if_alive(alive_, "update restart later", [this] {
            dialog_open_ = false;
            printf("[update] restart later (v%s installed)\n", pending_ver_.c_str());
            set_update_status("Updated to v" + pending_ver_ + " \xe2\x80\x94 restart to apply");
        }),
        WsAction::Confirm, "Restart now", "Later"));
}

void SettingsPane::start_install() {
    std::string url = updater_->asset_url();
    printf("[update] confirm -> install v%s\n", pending_ver_.c_str());
    install_started_ = true;
    updater_->start_install(url);
    last_phase_ = UpdatePhase::Downloading;  // e.g. UP_NO_SELF returns before the next poll
    set_update_status("Downloading\xe2\x80\xa6");
}

void SettingsPane::restart_now() {
    char self_path[512] = {0};
    int rc = updater_self_path(self_path, sizeof(self_path));
    if (rc == UP_OK && envHasNextLoad()) {
        Result nrc = envSetNextLoad(self_path, nullptr);
        if (R_SUCCEEDED(nrc)) {
            printf("[update] restart now: envSetNextLoad(%s) + quit\n", self_path);
            set_update_status("Restarting\xe2\x80\xa6");
            brls::Application::quit();  // normal teardown; the hbloader relaunches us
            return;
        }
        printf("[update] envSetNextLoad FAILED rc=%d\n", (int)nrc);
    } else {
        printf("[update] restart unavailable (self rc=%d, hasNextLoad=%d)\n",
               rc, (int)envHasNextLoad());
    }
    set_update_status("Updated to v" + pending_ver_ + ". Restart to apply.");
}

void SettingsPane::poll_update() {
    if (!updater_) return;
    UpdatePhase phase = updater_->phase();

    if (phase == UpdatePhase::Downloading)
        set_update_status("Downloading\xe2\x80\xa6 " + std::to_string(updater_->progress_percent()) + "%");

    if (last_phase_ == UpdatePhase::Checking && (phase == UpdatePhase::Done || phase == UpdatePhase::Error)) {
        int rc = updater_->check_rc();
        if (rc == UP_OK) {
            std::string ver = updater_->latest_version();
            if (version_newer(ver.c_str(), WS_APP_VERSION)) {
                pending_ver_ = ver;
                set_update_status("Update available: v" + ver);
                printf("[update] newer version available: v%s\n", ver.c_str());
                if (!dialog_open_) offer_install();
            } else {
                set_update_status("Up to date (v" + ver + ")");
                printf("[update] up to date (latest v%s)\n", ver.c_str());
            }
        } else {
            set_update_status(updater_check_message(rc, ".nro"));
            printf("[update] check failed rc=%d\n", rc);
        }
    }

    if (install_started_ && last_phase_ == UpdatePhase::Downloading &&
        (phase == UpdatePhase::Done || phase == UpdatePhase::Error)) {
        int rc = updater_->install_rc();
        if (updater_->cancelled()) {
            set_update_status("Update cancelled");
            printf("[update] install cancelled\n");
        } else if (rc == UP_OK) {
            set_update_status("Updated to v" + pending_ver_);
            printf("[update] install succeeded -> restart prompt\n");
            if (!dialog_open_) offer_restart();
        } else {
            set_update_status(updater_install_message(rc));
            printf("[update] install failed rc=%d\n", rc);
        }
        install_started_ = false;
    }

    last_phase_ = phase;
}
