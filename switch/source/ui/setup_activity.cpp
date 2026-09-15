#include "setup_activity.h"
#include "recovery_key_activity.h"
#include "session_store.h"
#include <borealis.hpp>
#include <cstdio>

extern "C" {
#include "waystone.h"
}

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

SetupActivity::~SetupActivity() {
    poll_fn_ = nullptr;
    if (vault_thread_.joinable()) vault_thread_.join();
    // vault_result_.vault is non-null only if the worker succeeded but we were
    // destroyed before the poll lambda could transfer ownership to session_.
    if (vault_result_.vault) {
        ws_vault_free(vault_result_.vault);
        vault_result_.vault = nullptr;
    }
    // No explicit zeroize_secrets() here: if the poll lambda ran, secrets are
    // already moved into session_; if the worker is still running (early exit),
    // the worker's std::string copies are zeroized by the worker itself. The
    // base class ~WizardActivity zeroizes values_[] automatically.
}

std::vector<WizardStepDef> SetupActivity::get_steps() const { return setup_steps(); }

std::string SetupActivity::finish_label() const {
    return creating_vault_ ? "Creating vault\xe2\x80\xa6" : "Create Vault";
}

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
    if (creating_vault_) return;
    // Re-entry guard: creating_vault_ prevents a second worker spawn if the
    // user hammers the finish button while Argon2 is running.

    session_->config.server_url = values_[FIELD_SERVER];
    session_->config.username   = values_[FIELD_USERNAME];

    creating_vault_ = true;
    status_ = "Creating vault\xe2\x80\xa6 please wait";
    schedule_refresh();

    std::string passphrase_copy = values_[FIELD_PASSPHRASE];

    vault_done_.store(false);

    vault_thread_ = std::thread([this, pass = passphrase_copy]() mutable {
        VaultCreateResult res = create_vault(pass, session_);
        zeroize_string(pass);
        vault_result_ = std::move(res);
        vault_done_.store(true);
    });
    zeroize_string(passphrase_copy);

    // Called by the pump's on_tick on the main thread. The pump copies
    // poll_fn_ to a local std::function before invoking it, because this
    // lambda clears poll_fn_ on completion (preventing re-entry after the
    // worker is joined).
    poll_fn_ = [this]() {
        if (!vault_done_.load()) return;

        poll_fn_ = nullptr;

        if (vault_thread_.joinable()) vault_thread_.join();

        if (values_[FIELD_SERVER].empty()) {
            creating_vault_ = false;
            status_.clear();
            return;
        }

        // Transfer ownership of the vault pointer out of vault_result_ so the
        // destructor cannot double-free it if this activity is destroyed after
        // we push RecoveryKeyActivity (which takes session_->vault).
        WsVault*    vault_ptr     = vault_result_.vault;
        vault_result_.vault       = nullptr;
        std::string recovery_hex  = std::move(vault_result_.recovery_hex);
        std::string recovery_path = std::move(vault_result_.recovery_path);
        std::string err_msg       = std::move(vault_result_.error);

        creating_vault_ = false;
        status_.clear();

        if (!vault_ptr) {
            error_ = err_msg;
            schedule_refresh();
            return;
        }

        session_->vault          = vault_ptr;
        session_->dav.server_url = values_[FIELD_SERVER];
        session_->dav.user       = values_[FIELD_USERNAME];
        session_->dav.pass       = std::move(values_[FIELD_PASSWORD]);
        zeroize_secrets();

        persist_session(session_->vault, session_->dav.pass);

        brls::Application::pushActivity(
            new RecoveryKeyActivity(session_, recovery_hex, recovery_path));
        zeroize_string(recovery_hex);
    };
}
