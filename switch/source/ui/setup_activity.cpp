#include "setup_activity.h"
#include "swkbd_util.h"
#include "vault_helpers.h"
#include "recovery_key_activity.h"

static std::vector<WizardStepDef> setup_steps() {
    return {
        {"Welcome to Waystone",  "Press (R) to begin setup",                                    false, ""},
        {"Server URL",           "your Waystone server, e.g. https://dav.example.com",          false, "not set"},
        {"Username",             "your WebDAV username",                                         false, "not set"},
        {"WebDAV Password",      "your WebDAV password (stored in RAM only)",                   true,  "not set"},
        {"Vault Passphrase",     "choose a strong passphrase to encrypt your saves",            true,  "not set"},
        {"Confirm Passphrase",   "re-enter your vault passphrase",                              true,  "not set"},
    };
}

SetupActivity::SetupActivity(Session* session)
    : WizardActivity(NUM_STEPS), session_(session) {}

std::vector<WizardStepDef> SetupActivity::get_steps() const { return setup_steps(); }

std::string SetupActivity::finish_label() const { return "Create Vault"; }

std::string SetupActivity::wizard_title() const { return "Waystone Setup"; }

void SetupActivity::edit_current_field() {
    if (current_step_ == STEP_WELCOME) return;

    auto steps = get_steps();
    const auto& step = steps[current_step_];
    std::string result = swkbd_prompt(step.label.c_str(), values_[current_step_], step.is_secret);
    if (!result.empty() || current_step_ < STEP_PASSWORD) {
        if (step.is_secret) zeroize_string(values_[current_step_]);
        values_[current_step_] = std::move(result);
    } else {
        zeroize_string(result);
    }
    error_.clear();
    refresh();
}

bool SetupActivity::validate_step(size_t step) {
    error_.clear();
    switch (step) {
        case STEP_SERVER:
            if (values_[STEP_SERVER].empty()) {
                error_ = "Server URL is required";
                return false;
            }
            if (values_[STEP_SERVER].rfind("http://", 0) != 0 &&
                values_[STEP_SERVER].rfind("https://", 0) != 0) {
                error_ = "Server URL must start with http:// or https://";
                return false;
            }
            return true;
        case STEP_USERNAME:
            if (values_[STEP_USERNAME].empty()) {
                error_ = "Username is required";
                return false;
            }
            return true;
        case STEP_PASSWORD:
            if (values_[STEP_PASSWORD].empty()) {
                error_ = "WebDAV password is required";
                return false;
            }
            return true;
        case STEP_PASSPHRASE:
            if (values_[STEP_PASSPHRASE].empty()) {
                error_ = "Passphrase is required";
                return false;
            }
            return true;
        case STEP_CONFIRM:
            if (values_[STEP_CONFIRM].empty()) {
                error_ = "Please confirm your passphrase";
                return false;
            }
            if (values_[STEP_CONFIRM] != values_[STEP_PASSPHRASE]) {
                error_ = "Passphrases do not match";
                zeroize_string(values_[STEP_CONFIRM]);
                return false;
            }
            return true;
        default:
            return true;
    }
}

void SetupActivity::on_finish() { do_create_vault(); }

void SetupActivity::do_create_vault() {
    session_->config.server_url = values_[STEP_SERVER];
    session_->config.username = values_[STEP_USERNAME];

    VaultCreateResult res = create_vault(values_[STEP_PASSPHRASE], session_);

    if (!res.vault) {
        error_ = res.error;
        refresh();
        return;
    }

    session_->vault = res.vault;
    session_->dav.server_url = values_[STEP_SERVER];
    session_->dav.user = values_[STEP_USERNAME];
    session_->dav.pass = std::move(values_[STEP_PASSWORD]);
    zeroize_secrets();

    brls::Application::pushActivity(
        new RecoveryKeyActivity(session_, res.recovery_hex, res.recovery_path));
    zeroize_string(res.recovery_hex);
}
