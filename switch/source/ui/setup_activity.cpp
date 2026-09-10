#include "setup_activity.h"
#include "recovery_key_activity.h"
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
    // Re-entry guard: worker thread already running.
    if (creating_vault_) return;

    printf("[vault] do_create_vault: entered\n");

    session_->config.server_url = values_[FIELD_SERVER];
    session_->config.username   = values_[FIELD_USERNAME];

    creating_vault_ = true;
    status_ = "Creating vault\xe2\x80\xa6 please wait";
    schedule_refresh();  // show the progress state before blocking

    // Copy passphrase for the worker; the original remains in values_ and is
    // zeroized by zeroize_secrets() in the poll callback on success.
    std::string passphrase_copy = values_[FIELD_PASSPHRASE];

    vault_done_.store(false);

    printf("[vault] do_create_vault: starting worker thread\n");

    vault_thread_ = std::thread([this, pass = passphrase_copy]() mutable {
        printf("[vault] worker: calling create_vault\n");
        VaultCreateResult res = create_vault(pass, session_);
        zeroize_string(pass);  // zeroize worker's passphrase copy immediately
        if (res.vault) {
            printf("[vault] worker: create_vault succeeded\n");
        } else {
            printf("[vault] worker: create_vault FAILED: %s\n", res.error.c_str());
        }
        vault_result_ = std::move(res);
        vault_done_.store(true);
    });
    zeroize_string(passphrase_copy);  // scrub the local; move leaves SSO remnants

    // poll_fn_ is called by RefreshPump on every 16ms tick (main thread only).
    // The pump copies before calling, so assigning poll_fn_ = nullptr here is safe.
    poll_fn_ = [this]() {
        if (!vault_done_.load()) return;

        poll_fn_ = nullptr;

        if (vault_thread_.joinable()) vault_thread_.join();

        // If the user pressed Exit while the worker was running,
        // zeroize_secrets() already cleared values_.  Skip result
        // processing — the destructor frees any orphaned vault.
        if (values_[FIELD_SERVER].empty()) {
            creating_vault_ = false;
            status_.clear();
            return;
        }

        printf("[vault] poll: worker joined, processing result\n");

        // Transfer ownership out of vault_result_ before touching any UI state.
        WsVault*    vault_ptr     = vault_result_.vault;
        vault_result_.vault       = nullptr;  // prevent destructor double-free
        std::string recovery_hex  = std::move(vault_result_.recovery_hex);
        std::string recovery_path = std::move(vault_result_.recovery_path);
        std::string err_msg       = std::move(vault_result_.error);

        creating_vault_ = false;
        status_.clear();

        if (!vault_ptr) {
            printf("[vault] poll: failure — showing error\n");
            error_ = err_msg;
            schedule_refresh();
            return;
        }

        printf("[vault] poll: success — setting up session\n");
        session_->vault          = vault_ptr;
        session_->dav.server_url = values_[FIELD_SERVER];
        session_->dav.user       = values_[FIELD_USERNAME];
        session_->dav.pass       = std::move(values_[FIELD_PASSWORD]);
        zeroize_secrets();

        printf("[vault] poll: before pushActivity(RecoveryKeyActivity)\n");
        brls::Application::pushActivity(
            new RecoveryKeyActivity(session_, recovery_hex, recovery_path));
        zeroize_string(recovery_hex);
        printf("[vault] poll: after pushActivity(RecoveryKeyActivity)\n");
    };
}
