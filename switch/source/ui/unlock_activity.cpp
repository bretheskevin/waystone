#include "unlock_activity.h"
#include "vault_helpers.h"

extern "C" {
struct Vault;
#include "waystone.h"
}

UnlockActivity::UnlockActivity(Session* session, const uint8_t* keys_data, size_t keys_len)
    : WizardActivity(NUM_VALUES), session_(session), keys_data_(keys_data), keys_len_(keys_len) {}

std::vector<WizardStepDef> UnlockActivity::get_steps() const {
    WizardFieldDef passphrase_field;
    if (recovery_mode_) {
        passphrase_field = {FIELD_PASSPHRASE, "Recovery Key", false, "not set",
                            "enter your hex recovery key (Y to switch to passphrase)"};
    } else {
        passphrase_field = {FIELD_PASSPHRASE, "Vault Passphrase", true, "not set",
                            "enter your vault passphrase (Y to switch to recovery key)"};
    }
    WizardFieldDef password_field = {FIELD_PASSWORD, "WebDAV Password", true, "not set",
                                     "your WebDAV password"};
    return {{"Unlock Vault", "", {passphrase_field, password_field}}};
}

std::string UnlockActivity::finish_label() const { return "Unlock"; }

std::string UnlockActivity::wizard_title() const { return "Unlock Vault"; }

bool UnlockActivity::validate_step(size_t /*step*/) {
    error_.clear();
    if (values_[FIELD_PASSPHRASE].empty()) {
        error_ = recovery_mode_ ? "Recovery key is required" : "Passphrase is required";
        return false;
    }
    if (values_[FIELD_PASSWORD].empty()) {
        error_ = "WebDAV password is required";
        return false;
    }
    return true;
}

void UnlockActivity::on_finish() { do_unlock(); }

void UnlockActivity::register_extra_actions() {
    registerAction("Toggle Recovery", brls::BUTTON_Y, [this](brls::View*) {
        if (current_step_ == 0) {
            recovery_mode_ = !recovery_mode_;
            zeroize_string(values_[FIELD_PASSPHRASE]);
            error_.clear();
            reload_steps();
            schedule_refresh();
        }
        return true;
    });
}

void UnlockActivity::do_unlock() {
    WsVault* vault = nullptr;

    if (recovery_mode_) {
        vault = ws_vault_unlock_recovery(values_[FIELD_PASSPHRASE].c_str(),
                                         keys_data_, keys_len_);
    } else {
        vault = ws_vault_unlock_pass(values_[FIELD_PASSPHRASE].c_str(),
                                     keys_data_, keys_len_);
    }

    if (!vault) {
        const char* err = ws_last_error();
        error_ = std::string("Unlock failed: ") + (err ? err : "wrong passphrase or key");
        zeroize_string(values_[FIELD_PASSPHRASE]);
        schedule_refresh();
        return;
    }

    session_->vault          = vault;
    session_->dav.server_url = session_->config.server_url;
    session_->dav.user       = session_->config.username;
    session_->dav.pass       = std::move(values_[FIELD_PASSWORD]);
    zeroize_string(values_[FIELD_PASSWORD]);
    zeroize_string(values_[FIELD_PASSPHRASE]);

    push_dashboard(session_);
}
