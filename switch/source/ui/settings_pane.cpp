#include "settings_pane.h"
#include "swkbd_util.h"
#include "session_store.h"
#include "keymap_switch.h"
#include "keys_file.h"
#include "unlock_activity.h"
#include "worker_reaper.h"
#include "pane_menu.h"
#include "version.h"
#include "kit/toast.h"
#include <cstdio>

extern "C" {
struct Vault;
#include "waystone.h"
}

static constexpr float LIST_MARGIN_TOP = 16.0f;

static const char* policy_name(WsConflictPolicy p) {
    return p == WsConflictPolicy::Prompt ? "Prompt" : "Newest Wins";
}

SettingsPane::SettingsPane(Session* session)
    : brls::Box(brls::Axis::COLUMN), session_(session),
      update_([this](const std::string& s) { update_row_->setDetail(s); }, "settings") {
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
    update_row_   = list_->addRow("Check for updates", "", [this] { update_.start_check(); });
    list_->addRow("Version", std::string("v") + WS_APP_VERSION)->setFocusable(false);
    list_->addRow("Save", "", [this] { save_settings(); });
    list_->addRow("Log out", "", [this] { request_logout(); });

    pump_.start();
    refresh_labels();
    printf("[ui] settings pane: created\n");
}

SettingsPane::~SettingsPane() {
    pump_.stop();
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
    session_drop_login(session_->vault, session_->dav.user, session_->dav.pass);
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
