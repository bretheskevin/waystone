#include "settings_activity.h"
#include "swkbd_util.h"
#include "session_store.h"
#include "keymap_switch.h"
#include "keys_file.h"
#include "unlock_activity.h"
#include "worker_reaper.h"
#include <cstdio>

extern "C" {
struct Vault;
#include "waystone.h"
}

SettingsActivity::SettingsActivity(Session* session) : session_(session) {}
SettingsActivity::~SettingsActivity() { pump_.stop(); }

brls::View* SettingsActivity::createContentView()
{
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(20.0f);

    auto* title = new brls::Label();
    title->setText("Settings");
    title->setFontSize(28.0f);
    title->setSingleLine(true);
    col->addView(title);

    server_label_ = new brls::Label();
    server_label_->setFontSize(20.0f);
    server_label_->setFocusable(true);
    server_label_->registerClickAction([this](brls::View*) {
        std::string val = swkbd_prompt("Server URL", session_->config.server_url, false);
        if (!val.empty()) {
            session_->config.server_url = val;
            session_->dav.server_url = val;
            zeroize_string(session_->dav.pass);
            refresh_labels();
        }
        return true;
    });
    col->addView(server_label_);

    user_label_ = new brls::Label();
    user_label_->setFontSize(20.0f);
    user_label_->setFocusable(true);
    user_label_->registerClickAction([this](brls::View*) {
        std::string val = swkbd_prompt("Username", session_->config.username, false);
        if (!val.empty()) {
            session_->config.username = val;
            session_->dav.user = val;
            refresh_labels();
        }
        return true;
    });
    col->addView(user_label_);

    policy_label_ = new brls::Label();
    policy_label_->setFontSize(20.0f);
    policy_label_->setFocusable(true);
    policy_label_->registerClickAction([this](brls::View*) {
        session_->config.conflict_policy =
            (session_->config.conflict_policy == WsConflictPolicy::NewestWins)
                ? WsConflictPolicy::Prompt
                : WsConflictPolicy::NewestWins;
        refresh_labels();
        return true;
    });
    col->addView(policy_label_);

    backup_label_ = new brls::Label();
    backup_label_->setFontSize(20.0f);
    backup_label_->setFocusable(true);
    backup_label_->registerClickAction([this](brls::View*) {
        session_->config.safety_backup = !session_->config.safety_backup;
        refresh_labels();
        return true;
    });
    col->addView(backup_label_);

    device_label_ = new brls::Label();
    device_label_->setFontSize(20.0f);
    col->addView(device_label_);

    save_label_ = new brls::Label();
    save_label_->setText("Save");
    save_label_->setFontSize(20.0f);
    save_label_->setFocusable(true);
    save_label_->registerClickAction([this](brls::View*) {
        save_settings();
        return true;
    });
    col->addView(save_label_);

    logout_label_ = new brls::Label();
    logout_label_->setText("Log out");
    logout_label_->setFontSize(20.0f);
    logout_label_->setFocusable(true);
    logout_label_->setTextColor(nvgRGB(220, 50, 50));
    logout_label_->registerClickAction([this](brls::View*) {
        if (session_->sync_busy && session_->sync_busy()) {
            printf("[ui] settings: log out refused -- sync in progress\n");
            status_label_->setText("Sync in progress \xe2\x80\x94 wait for it to finish");
            return true;
        }
        if (worker_reaper_pending() > 0) {
            printf("[ui] settings: log out refused -- %zu worker(s) still finishing\n", worker_reaper_pending());
            status_label_->setText("Finishing a background task \xe2\x80\x94 try again in a moment");
            return true;
        }
        if (logout_pending_) return true;
        printf("[ui] settings: log out requested (deferred one frame)\n");
        logout_pending_ = true;
        status_label_->setText("Logging out\xe2\x80\xa6");
        pump_.schedule();
        return true;
    });
    col->addView(logout_label_);

    status_label_ = new brls::Label();
    status_label_->setFontSize(18.0f);
    col->addView(status_label_);

    frame->setContentView(col);
    return frame;
}

void SettingsActivity::onContentAvailable()
{
    pump_.start();
    refresh_labels();

    registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [](brls::View*) {
        printf("[ui] settings back\n");
        brls::Application::popActivity();
        return true;
    });
}

void SettingsActivity::save_settings()
{
    printf("[ui] settings save -> %s\n", session_->config_path.c_str());
    if (wsconfig_save(session_->config, session_->config_path.c_str())) {
        printf("[ui] settings save ok\n");
        status_label_->setText("Settings saved");
    } else {
        printf("[ui] settings save FAILED\n");
        status_label_->setText("Failed to save settings");
    }
}

void SettingsActivity::refresh_labels()
{
    server_label_->setText("Server: " + session_->config.server_url);
    user_label_->setText("Username: " + session_->config.username);

    const char* policy_name =
        (session_->config.conflict_policy == WsConflictPolicy::Prompt) ? "Prompt" : "Newest Wins";
    policy_label_->setText(std::string("Conflict Policy: ") + policy_name);

    backup_label_->setText(std::string("Safety Backup: ")
        + (session_->config.safety_backup ? "ON" : "OFF"));

    device_label_->setText("Device ID: " + session_->device_id + " (read-only)");
}

void SettingsActivity::do_logout()
{
    if (!logout_pending_) return;
    logout_pending_ = false;
    printf("[vault] logout: clearing session + freeing vault\n");
    session_store_clear();
    if (session_->vault) {
        ws_vault_free(session_->vault);
        session_->vault = nullptr;
    }
    zeroize_string(session_->dav.pass);   // in place: OwnedWebDavCfg is non-movable
    zeroize_string(session_->dav.user);
    long klen = 0;
    uint8_t* kbuf = read_keys_file("sdmc:/waystone/keys.json", &klen);
    if (!kbuf || klen <= 0) {
        printf("[vault] logout: keys.json unreadable -- staying on Settings\n");
        status_label_->setText("Logged out. Please restart the app.");
        return;
    }
    // kbuf intentionally never freed: UnlockActivity borrows it for the app lifetime (3DS does the same).
    printf("[ui] logout -> push UnlockActivity (activities below stay; not popped)\n");
    brls::Application::pushActivity(new UnlockActivity(session_, kbuf, static_cast<size_t>(klen)));
}
