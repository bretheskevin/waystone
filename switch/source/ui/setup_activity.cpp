#include "setup_activity.h"
#include "vault_helpers.h"
#include "recovery_key_activity.h"

static std::vector<WizardStepDef> setup_steps() {
    return {
        {"Welcome to Waystone", "Press (R) to begin setup", {}},
        {"Server URL",          "",                          {
            {0, "Server URL", false, "not set",
             "your Waystone server, e.g. https://dav.example.com"},
        }},
        {"WebDAV Login",        "",                          {
            {1, "WebDAV Username", false, "not set", "your WebDAV username"},
            {2, "WebDAV Password", true,  "not set", "your WebDAV password (stored in RAM only)"},
        }},
        {"Vault Passphrase",    "",                          {
            {3, "Passphrase",         true, "not set",
             "choose a strong passphrase to encrypt your saves"},
            {4, "Confirm Passphrase", true, "not set",
             "re-enter your vault passphrase"},
        }},
    };
}

SetupActivity::SetupActivity(Session* session)
    : WizardActivity(NUM_VALUES), session_(session) {}

std::vector<WizardStepDef> SetupActivity::get_steps() const { return setup_steps(); }

std::string SetupActivity::finish_label() const { return "Create Vault"; }

std::string SetupActivity::wizard_title() const { return "Waystone Setup"; }

bool SetupActivity::validate_step(size_t step) {
    error_.clear();
    switch (step) {
        case 1:  // Server URL
            if (values_[FIELD_SERVER].empty()) {
                error_ = "Server URL is required";
                return false;
            }
            if (values_[FIELD_SERVER].rfind("http://", 0) != 0 &&
                values_[FIELD_SERVER].rfind("https://", 0) != 0) {
                error_ = "Server URL must start with http:// or https://";
                return false;
            }
            return true;
        case 2:  // WebDAV Login
            if (values_[FIELD_USERNAME].empty()) {
                error_ = "WebDAV Username is required";
                return false;
            }
            if (values_[FIELD_PASSWORD].empty()) {
                error_ = "WebDAV Password is required";
                return false;
            }
            return true;
        case 3:  // Vault Passphrase
            if (values_[FIELD_PASSPHRASE].empty()) {
                error_ = "Passphrase is required";
                return false;
            }
            if (values_[FIELD_CONFIRM].empty()) {
                error_ = "Please confirm your passphrase";
                return false;
            }
            if (values_[FIELD_CONFIRM] != values_[FIELD_PASSPHRASE]) {
                error_ = "Passphrases do not match";
                zeroize_string(values_[FIELD_CONFIRM]);
                return false;
            }
            return true;
        default:
            return true;
    }
}

void SetupActivity::on_finish() { do_create_vault(); }

void SetupActivity::do_create_vault() {
    session_->config.server_url = values_[FIELD_SERVER];
    session_->config.username   = values_[FIELD_USERNAME];

    VaultCreateResult res = create_vault(values_[FIELD_PASSPHRASE], session_);

    if (!res.vault) {
        error_ = res.error;
        schedule_refresh();
        return;
    }

    session_->vault          = res.vault;
    session_->dav.server_url = values_[FIELD_SERVER];
    session_->dav.user       = values_[FIELD_USERNAME];
    session_->dav.pass       = std::move(values_[FIELD_PASSWORD]);
    zeroize_secrets();

    brls::Application::pushActivity(
        new RecoveryKeyActivity(session_, res.recovery_hex, res.recovery_path));
    zeroize_string(res.recovery_hex);
}
