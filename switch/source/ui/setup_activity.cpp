#include "setup_activity.h"
#include "recovery_key_activity.h"
#include "session_store.h"
#include "keymap_switch.h"
#include "ffi_err.h"
#include <borealis.hpp>
#include <cstdio>

SetupActivity::SetupActivity(Session* session)
    : WizardActivity(NUM_VALUES), session_(session) {
    values_[FIELD_SERVER]   = session->config.server_url;
    values_[FIELD_USERNAME] = session->config.username;
    setup_log_prefill(values_[FIELD_SERVER], values_[FIELD_USERNAME]);
}

SetupActivity::~SetupActivity() {
    poll_fn_ = nullptr;
    if (worker_.joinable()) worker_.join();
    if (setup_result_.vault) {
        ws_vault_free(setup_result_.vault);
        setup_result_.vault = nullptr;
    }
    vault_setup_wipe_recovery(&setup_result_);
}

std::vector<WizardStepDef> SetupActivity::get_steps() const {
    std::vector<WizardStepDef> steps = {
        {setup_welcome_title(), setup_welcome_hint(ws_glyph(ws_button(WsAction::WizardNext))), {}},
        {"Server URL",          "",                          {
            {FIELD_SERVER, "Server URL", false, "not set",
             "your Waystone server, e.g. https://dav.example.com"},
        }},
        {"WebDAV Login",        "",                          {
            {FIELD_USERNAME, "WebDAV Username", false, "not set", "your WebDAV username"},
            {FIELD_PASSWORD, "WebDAV Password", true,  "not set",
             "your WebDAV password (stored in RAM only)"},
        }},
    };
    const char* toggle_glyph = ws_glyph(ws_button(WsAction::ToggleEncryption));
    if (mode_ == SetupVaultMode::Join) {
        steps.push_back({setup_vault_step_title(mode_), "",
                         {vault_secret_field(FIELD_PASSPHRASE, join_recovery_)}});
    } else if (mode_ == SetupVaultMode::Create) {
        steps.push_back({setup_vault_step_title(mode_), "", {
            {FIELD_PASSPHRASE, setup_passphrase_label(), true, "not set",
             setup_vault_step_hint(mode_, toggle_glyph)},
            {FIELD_CONFIRM,    setup_confirm_label(),    true, "not set", setup_confirm_hint()},
        }});
    } else {
        steps.push_back({setup_vault_step_title(mode_), setup_vault_step_hint(mode_, toggle_glyph),
                         {}});
    }
    return steps;
}

std::string SetupActivity::finish_label() const { return setup_finish_label(mode_, busy_); }

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
        case STEP_LOGIN:
            if (probe_passed_) {
                probe_passed_ = false;
                return true;
            }
            if (values_[FIELD_USERNAME].empty()) {
                error_ = "WebDAV Username is required";
                return false;
            }
            if (values_[FIELD_PASSWORD].empty()) {
                error_ = "WebDAV Password is required";
                return false;
            }
            start_probe();  // advances from on_probe_done
            return false;
        case STEP_VAULT:
            if (setup_mode_is_plain(mode_)) return true;
            if (values_[FIELD_PASSPHRASE].empty()) {
                error_ = vault_secret_required(mode_ == SetupVaultMode::Join && join_recovery_);
                return false;
            }
            if (mode_ == SetupVaultMode::Join) return true;
            if (values_[FIELD_CONFIRM].empty()) {
                error_ = setup_confirm_required();
                return false;
            }
            if (values_[FIELD_CONFIRM] != values_[FIELD_PASSPHRASE]) {
                error_ = setup_passphrase_mismatch();
                zeroize_string(values_[FIELD_CONFIRM]);
                return false;
            }
            return true;
        default:
            return true;
    }
}

void SetupActivity::on_finish() { start_setup(); }

std::vector<WsAction> SetupActivity::extra_hint_actions() const {
    return setup_extra_actions(current_step_);
}

void SetupActivity::register_extra_actions() {
    register_update_check(&update_, "setup", [this] { return setup_step_offers_updates(current_step_) && !busy_; });
    register_recovery_toggle(&join_recovery_, FIELD_PASSPHRASE, [this] {
        return current_step_ == STEP_VAULT && mode_ == SetupVaultMode::Join && !busy_;
    });
    // borealis keeps one action per button: when ToggleEncryption shares ToggleRecovery's button
    // this registration replaces it, so the Join case is forwarded here.
    registerAction(ws_label(WsAction::ToggleEncryption), ws_brls(WsAction::ToggleEncryption),
                   [this](brls::View*) {
        if (current_step_ != STEP_VAULT || busy_) return true;
        if (mode_ == SetupVaultMode::Join &&
            ws_button(WsAction::ToggleEncryption) == ws_button(WsAction::ToggleRecovery)) {
            toggle_recovery(&join_recovery_, FIELD_PASSPHRASE);
        } else if (!setup_mode_is_join(mode_)) {
            toggle_encryption();
        }
        return true;
    });
}

void SetupActivity::toggle_encryption() {
    if (mode_ == SetupVaultMode::Create) {
        zeroize_string(values_[FIELD_PASSPHRASE]);
        zeroize_string(values_[FIELD_CONFIRM]);
    }
    mode_ = setup_toggle_encryption(mode_);
    error_.clear();
    reload_steps();
    schedule_refresh();
}

void SetupActivity::run_worker(std::function<void()> work, std::function<void()> on_done) {
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    worker_done_.store(false);
    worker_ = std::thread([this, work] {
        work();
        worker_done_.store(true);
    });
    // The pump copies poll_fn_ before calling it, so clearing it here is safe.
    poll_fn_ = [this, on_done] {
        if (!worker_done_.load()) return;
        poll_fn_ = nullptr;
        if (worker_.joinable()) worker_.join();
        busy_ = false;
        status_.clear();
        on_done();
    };
}

void SetupActivity::start_probe() {
    printf("[ui] setup: probing server for an existing vault\n");
    status_ = setup_probe_status();
    run_worker([this] {
        WebDavCfg cfg{values_[FIELD_SERVER].c_str(), values_[FIELD_USERNAME].c_str(),
                      values_[FIELD_PASSWORD].c_str()};
        probe_ = vault_fetch_remote_keys(cfg);
    }, [this] { on_probe_done(); });
}

void SetupActivity::on_probe_done() {
    if (probe_.state == RemoteKeysState::Error) {
        printf("[ui] setup: probe failed (http=%d, err=%s) - staying on login step\n",
               probe_.http_status, probe_.error.empty() ? "-" : probe_.error.c_str());
        error_ = setup_probe_error(probe_.http_status, probe_.error);
        schedule_refresh();
        return;
    }
    bool present = probe_.state == RemoteKeysState::Present;
    printf("[ui] setup: probe mode=%s\n",
           present ? keys_mode_name(probe_.plain) : "absent");
    SetupVaultMode mode = setup_mode_after_probe(present, probe_.plain, mode_);
    printf("[ui] setup: probe -> %s step\n", setup_vault_step_title(mode));
    if (mode != mode_) {
        mode_          = mode;
        join_recovery_ = false;
        zeroize_string(values_[FIELD_PASSPHRASE]);
        zeroize_string(values_[FIELD_CONFIRM]);
        reload_steps();
    }
    probe_passed_ = true;
    go_next();
}

void SetupActivity::start_setup() {
    if (busy_) return;
    bool join  = setup_mode_is_join(mode_);
    bool plain = setup_mode_is_plain(mode_);
    session_->config.server_url = values_[FIELD_SERVER];
    session_->config.username   = values_[FIELD_USERNAME];

    printf("[ui] setup: %s %s start\n", join ? "join" : "create", keys_mode_name(plain));
    status_ = setup_work_status(mode_);
    run_worker([this, join, plain] {
        const char* secret = plain ? nullptr : values_[FIELD_PASSPHRASE].c_str();
        if (join) {
            setup_result_ = setup_join_vault(session_, probe_.bytes, secret,
                                             !plain && join_recovery_);
        } else {
            WebDavCfg cfg{values_[FIELD_SERVER].c_str(), values_[FIELD_USERNAME].c_str(),
                          values_[FIELD_PASSWORD].c_str()};
            setup_result_ = setup_create_vault(session_, cfg, secret);
        }
    }, [this, join] { on_setup_done(join); });
    schedule_refresh();
}

void SetupActivity::on_setup_done(bool join) {
    WsVault* vault      = setup_result_.vault;
    setup_result_.vault = nullptr;
    if (!vault) {
        printf("[ui] setup: %s failed: %s\n", join ? "join" : "create", setup_result_.error.c_str());
        error_ = setup_result_.error;
        schedule_refresh();
        return;
    }
    bool to_dashboard = !setup_mode_shows_recovery_key(mode_);
    printf("[ui] setup: %s ok -> %s\n", join ? "join" : "create",
           to_dashboard ? "dashboard" : "recovery key");

    session_->vault          = vault;
    session_->dav.server_url = values_[FIELD_SERVER];
    session_->dav.user       = values_[FIELD_USERNAME];
    session_->dav.pass       = values_[FIELD_PASSWORD];  // copy: zeroize_secrets wipes the source
    zeroize_secrets();
    probe_.bytes.clear();

    persist_session(session_->vault, session_->dav.pass);

    if (to_dashboard) {
        vault_setup_wipe_recovery(&setup_result_);
        push_dashboard(session_);
        return;
    }
    brls::Application::pushActivity(
        new RecoveryKeyActivity(session_, setup_result_.recovery_hex, ws_recovery_path(session_->device_id)));
    vault_setup_wipe_recovery(&setup_result_);
}
