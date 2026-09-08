#include "unlock_activity.h"
#include "swkbd_util.h"
#include "title_list_activity.h"
#include "sync_controller.h"
#include <cstdio>

extern "C" {
struct Vault;
#include "waystone.h"
}

UnlockActivity::UnlockActivity(Session* session, const uint8_t* keys_data, size_t keys_len)
    : session_(session), keys_data_(keys_data), keys_len_(keys_len) {}

UnlockActivity::~UnlockActivity() = default;

brls::View* UnlockActivity::createContentView()
{
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(20.0f);

    auto* title = new brls::Label();
    title->setText("Waystone Unlock");
    title->setFontSize(28.0f);
    title->setSingleLine(true);
    col->addView(title);

    status_label_ = new brls::Label();
    status_label_->setText("Press A to unlock (Y to toggle recovery mode)");
    status_label_->setFontSize(20.0f);
    col->addView(status_label_);

    frame->setContentView(col);
    return frame;
}

void UnlockActivity::onContentAvailable()
{
    registerAction("Unlock", brls::BUTTON_A, [this](brls::View*) { attempt_unlock(); return true; });
    registerAction("Toggle Recovery", brls::BUTTON_Y, [this](brls::View*) {
        recovery_mode_ = !recovery_mode_;
        status_label_->setText(recovery_mode_
            ? "RECOVERY MODE -- Press A to unlock"
            : "Press A to unlock (Y to toggle recovery mode)");
        return true;
    });
}

void UnlockActivity::attempt_unlock()
{
    WsVault* vault = nullptr;

    if (recovery_mode_) {
        std::string recovery_key = swkbd_prompt("Recovery Key (hex)", "", false);
        if (recovery_key.empty()) { status_label_->setText("Cancelled"); return; }
        vault = ws_vault_unlock_recovery(recovery_key.c_str(), keys_data_, keys_len_);
        zeroize_string(recovery_key);
    } else {
        std::string passphrase = swkbd_prompt("Vault Passphrase", "", true);
        if (passphrase.empty()) { status_label_->setText("Cancelled"); return; }
        vault = ws_vault_unlock_pass(passphrase.c_str(), keys_data_, keys_len_);
        zeroize_string(passphrase);
    }

    if (!vault) {
        const char* err = ws_last_error();
        status_label_->setText(std::string("Unlock failed: ")
            + (err ? err : "wrong passphrase or key"));
        return;
    }

    std::string password = swkbd_prompt("WebDAV Password", "", true);
    if (password.empty()) {
        zeroize_string(password);
        ws_vault_free(vault);
        status_label_->setText("Cancelled (no password)");
        return;
    }

    session_->vault = vault;
    session_->dav.server_url = session_->config.server_url;
    session_->dav.user = session_->config.username;
    session_->dav.pass = password;
    zeroize_string(password);

    auto titles = list_titles();
    auto* ctrl = new SyncController(session_->vault, session_->uid, session_->device_id,
                                    session_->dav.as_cfg(), std::move(titles));
    brls::Application::pushActivity(new TitleListActivity(ctrl, session_));
}
