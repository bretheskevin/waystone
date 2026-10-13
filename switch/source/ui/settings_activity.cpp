#include "settings_activity.h"
#include "swkbd_util.h"
#include "session_store.h"
#include "keymap_switch.h"
#include "keys_file.h"
#include "unlock_activity.h"
#include "worker_reaper.h"
#include "confirm_banner.h"
#include "version.h"
#include "updater.h"
#include <switch.h>
#include <cstdio>

extern "C" {
struct Vault;
#include "waystone.h"
}

SettingsActivity::SettingsActivity(Session* session) : session_(session), updater_(new UpdateController()) {}
SettingsActivity::~SettingsActivity()
{
    pump_.stop();
    reap_worker(updater_);
}

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
    col_ = col;

    version_label_ = new brls::Label();
    version_label_->setText(std::string("Waystone v") + WS_APP_VERSION);
    version_label_->setFontSize(18.0f);
    version_label_->setSingleLine(true);
    version_label_->setTextColor(nvgRGB(150, 150, 150));
    col->addView(version_label_);

    server_label_ = new brls::Label();
    server_label_->setFontSize(20.0f);
    server_label_->setFocusable(true);
    server_label_->registerClickAction([this](brls::View*) {
        if (banner_active()) return false;  // banner owns A while it is up
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
        if (banner_active()) return false;
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
        if (banner_active()) return false;
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
        if (banner_active()) return false;
        session_->config.safety_backup = !session_->config.safety_backup;
        refresh_labels();
        return true;
    });
    col->addView(backup_label_);

    update_label_ = new brls::Label();
    update_label_->setText("Check for updates");
    update_label_->setFontSize(20.0f);
    update_label_->setFocusable(true);
    update_label_->registerClickAction([this](brls::View*) {
        if (banner_active()) return false;
        if (updater_->is_running()) return true;
        printf("[update] check requested from settings\n");
        install_started_ = false;
        updater_->start_check();
        last_phase_ = UpdatePhase::Checking;  // a check that fails before the next poll still reports
        status_label_->setText("Checking for updates\xe2\x80\xa6");
        return true;
    });
    col->addView(update_label_);

    device_label_ = new brls::Label();
    device_label_->setFontSize(20.0f);
    col->addView(device_label_);

    save_label_ = new brls::Label();
    save_label_->setText("Save");
    save_label_->setFontSize(20.0f);
    save_label_->setFocusable(true);
    save_label_->registerClickAction([this](brls::View*) {
        if (banner_active()) return false;
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
        if (banner_active()) return false;
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

    // A: confirm whatever banner is up (install the update / restart into it).
    // Rows return false while a banner is up so the press propagates here.
    registerAction(ws_label(WsAction::Update), ws_brls(WsAction::Update), [this](brls::View*) {
        if (restart_prompt_) {
            restart_now();
            return true;
        }
        if (confirm_update_) {
            start_install();
            return true;
        }
        return false;
    });

    // B: decline the banner if one is up, else leave Settings.
    registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [this](brls::View*) {
        if (restart_prompt_) {
            printf("[update] restart later (v%s installed)\n", pending_ver_.c_str());
            restart_prompt_ = false;
            status_label_->setText("Updated to v" + pending_ver_ + " \xe2\x80\x94 restart to apply");
            pump_.schedule();
            return true;
        }
        if (confirm_update_) {
            printf("[update] confirm declined (back)\n");
            confirm_update_ = false;
            status_label_->setText("Update postponed");
            pump_.schedule();
            return true;
        }
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

void SettingsActivity::start_install()
{
    std::string url = updater_->asset_url();
    printf("[update] confirm -> install v%s\n", pending_ver_.c_str());
    confirm_update_ = false;
    install_started_ = true;
    updater_->start_install(url);
    last_phase_ = UpdatePhase::Downloading;  // e.g. UP_NO_SELF returns before the next poll
    status_label_->setText("Downloading\xe2\x80\xa6");
    pump_.schedule();
}

void SettingsActivity::restart_now()
{
    char self_path[512] = {0};
    int rc = updater_self_path(self_path, sizeof(self_path));
    if (rc == UP_OK && envHasNextLoad()) {
        Result nrc = envSetNextLoad(self_path, nullptr);
        if (R_SUCCEEDED(nrc)) {
            printf("[update] restart now: envSetNextLoad(%s) + quit\n", self_path);
            status_label_->setText("Restarting\xe2\x80\xa6");
            brls::Application::quit();  // normal teardown; the hbloader relaunches us
            return;
        }
        printf("[update] envSetNextLoad FAILED rc=%d\n", (int)nrc);
    } else {
        printf("[update] restart unavailable (self rc=%d, hasNextLoad=%d)\n",
               rc, (int)envHasNextLoad());
    }
    restart_prompt_ = false;
    status_label_->setText("Updated to v" + pending_ver_ + ". Restart to apply.");
    pump_.schedule();
}

void SettingsActivity::refresh_banner()
{
    if (banner_active() && !banner_) {
        banner_ = restart_prompt_
            ? make_confirm_banner("Waystone v" + pending_ver_ + " installed", "Restart now to apply?")
            : make_confirm_banner("Waystone v" + pending_ver_ + " available", "Download and install now?");
        col_->addView(banner_, 2);
    } else if (!banner_active() && banner_) {
        col_->removeView(banner_);
        banner_ = nullptr;
    }
}

void SettingsActivity::poll_update()
{
    if (!updater_) return;
    UpdatePhase phase = updater_->phase();

    if (phase == UpdatePhase::Downloading)
        status_label_->setText("Downloading\xe2\x80\xa6 " + std::to_string(updater_->progress_percent()) + "%");

    if (last_phase_ == UpdatePhase::Checking && (phase == UpdatePhase::Done || phase == UpdatePhase::Error)) {
        int rc = updater_->check_rc();
        if (rc == UP_OK) {
            std::string ver = updater_->latest_version();
            if (version_newer(ver.c_str(), WS_APP_VERSION)) {
                pending_ver_ = ver;
                confirm_update_ = true;
                status_label_->setText("Update available: v" + ver);
                printf("[update] newer version available: v%s\n", ver.c_str());
                pump_.schedule();
            } else {
                status_label_->setText("Up to date (v" + ver + ")");
                printf("[update] up to date (latest v%s)\n", ver.c_str());
            }
        } else {
            status_label_->setText(updater_check_message(rc, ".nro"));
            printf("[update] check failed rc=%d\n", rc);
        }
    }

    if (install_started_ && last_phase_ == UpdatePhase::Downloading &&
        (phase == UpdatePhase::Done || phase == UpdatePhase::Error)) {
        int rc = updater_->install_rc();
        if (updater_->cancelled()) {
            status_label_->setText("Update cancelled");
            printf("[update] install cancelled\n");
        } else if (rc == UP_OK) {
            restart_prompt_ = true;
            status_label_->setText("Updated to v" + pending_ver_);
            printf("[update] install succeeded -> restart prompt\n");
            pump_.schedule();
        } else {
            status_label_->setText(updater_install_message(rc));
            printf("[update] install failed rc=%d\n", rc);
        }
        install_started_ = false;
    }

    last_phase_ = phase;
}
