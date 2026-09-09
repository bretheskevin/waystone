#include "unlock_activity.h"
#include "swkbd_util.h"
#include "vault_helpers.h"

extern "C" {
struct Vault;
#include "waystone.h"
}

UnlockActivity::UnlockActivity(Session* session, const uint8_t* keys_data, size_t keys_len)
    : WizardActivity(NUM_STEPS), session_(session), keys_data_(keys_data), keys_len_(keys_len) {}

std::vector<WizardStepDef> UnlockActivity::get_steps() const {
    if (recovery_mode_) {
        return {
            {"Recovery Key",    "enter your hex recovery key (Y to switch to passphrase)", false, "not set"},
            {"WebDAV Password", "your WebDAV password",                                    true,  "not set"},
        };
    }
    return {
        {"Vault Passphrase", "enter your vault passphrase (Y to switch to recovery key)", true,  "not set"},
        {"WebDAV Password",  "your WebDAV password",                                      true,  "not set"},
    };
}

std::string UnlockActivity::finish_label() const { return "Unlock"; }

std::string UnlockActivity::wizard_title() const { return "Unlock Vault"; }

bool UnlockActivity::validate_step(size_t step) {
    error_.clear();
    if (step == STEP_PASSPHRASE && values_[STEP_PASSPHRASE].empty()) {
        error_ = recovery_mode_ ? "Recovery key is required" : "Passphrase is required";
        return false;
    }
    if (step == STEP_PASSWORD && values_[STEP_PASSWORD].empty()) {
        error_ = "WebDAV password is required";
        return false;
    }
    return true;
}

void UnlockActivity::on_finish() { do_unlock(); }

void UnlockActivity::register_extra_actions() {
    registerAction("Toggle Recovery", brls::BUTTON_Y, [this](brls::View*) {
        if (current_step_ == STEP_PASSPHRASE) {
            recovery_mode_ = !recovery_mode_;
            zeroize_string(values_[STEP_PASSPHRASE]);
            error_.clear();
            delete renderer_;
            renderer_ = new WizardRenderer(content_box_, get_steps());
            refresh();
        }
        return true;
    });
}

void UnlockActivity::edit_current_field() {
    auto steps = get_steps();
    const auto& step = steps[current_step_];
    std::string result = swkbd_prompt(step.label.c_str(), values_[current_step_], step.is_secret);
    if (!result.empty()) {
        if (step.is_secret) zeroize_string(values_[current_step_]);
        values_[current_step_] = std::move(result);
    } else {
        zeroize_string(result);
    }
    error_.clear();
    schedule_refresh();
}

void UnlockActivity::do_unlock() {
    WsVault* vault = nullptr;

    if (recovery_mode_) {
        vault = ws_vault_unlock_recovery(values_[STEP_PASSPHRASE].c_str(),
                                         keys_data_, keys_len_);
    } else {
        vault = ws_vault_unlock_pass(values_[STEP_PASSPHRASE].c_str(),
                                     keys_data_, keys_len_);
    }

    if (!vault) {
        const char* err = ws_last_error();
        error_ = std::string("Unlock failed: ") + (err ? err : "wrong passphrase or key");
        zeroize_string(values_[STEP_PASSPHRASE]);
        refresh();
        return;
    }

    session_->vault = vault;
    session_->dav.server_url = session_->config.server_url;
    session_->dav.user = session_->config.username;
    session_->dav.pass = std::move(values_[STEP_PASSWORD]);
    zeroize_string(values_[STEP_PASSWORD]);
    zeroize_string(values_[STEP_PASSPHRASE]);

    push_dashboard(session_);
}
