#include "unlock_activity.h"
#include "loading_activity.h"
#include "vault_helpers.h"
#include "session_store.h"
#include <cstdio>

extern "C" {
struct Vault;
#include "waystone.h"
}

UnlockActivity::UnlockActivity(Session* session, const uint8_t* keys_data, size_t keys_len)
    : WizardActivity(NUM_VALUES), session_(session), keys_data_(keys_data), keys_len_(keys_len) {}

std::vector<WizardStepDef> UnlockActivity::get_steps() const {
    WizardFieldDef password_field = {FIELD_PASSWORD, "WebDAV Password", true, "not set",
                                     "your WebDAV password"};
    return {{"Unlock Vault", "",
             {vault_secret_field(FIELD_PASSPHRASE, recovery_mode_), password_field}}};
}

std::string UnlockActivity::finish_label() const { return "Unlock"; }

std::string UnlockActivity::wizard_title() const { return "Unlock Vault"; }

bool UnlockActivity::validate_step(size_t /*step*/) {
    error_.clear();
    if (values_[FIELD_PASSPHRASE].empty()) {
        error_ = vault_secret_required(recovery_mode_);
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
    register_recovery_toggle(&recovery_mode_, FIELD_PASSPHRASE,
                             [this] { return current_step_ == 0; });
}

// Task 5 + 7b: run Argon2 KDF + title listing off the render thread; persist session.
void UnlockActivity::do_unlock() {
    std::string passphrase_copy = values_[FIELD_PASSPHRASE];
    std::string password_copy   = values_[FIELD_PASSWORD];
    bool recovery = recovery_mode_;
    std::vector<uint8_t> keys_copy(keys_data_, keys_data_ + keys_len_);
    Session* session = session_;

    auto worker = [session, pass = std::move(passphrase_copy),
                   pwd = std::move(password_copy),
                   keys = std::move(keys_copy), recovery]() mutable
        -> LoadingActivity::LoadResult {
        WsVault* vault = nullptr;
        if (recovery) {
            vault = ws_vault_unlock_recovery(pass.c_str(), keys.data(), keys.size());
        } else {
            vault = ws_vault_unlock_pass(pass.c_str(), keys.data(), keys.size());
        }
        zeroize_string(pass);

        if (!vault) {
            const char* err = ws_last_error();
            std::string msg = std::string("Unlock failed: ") + (err ? err : "wrong passphrase or key");
            zeroize_string(pwd);
            return {false, std::move(msg), {}};
        }

        session->vault          = vault;
        session->dav.server_url = session->config.server_url;
        session->dav.user       = session->config.username;
        session->dav.pass       = std::move(pwd);
        zeroize_string(pwd);

        persist_session(session->vault, session->dav.pass);

        return {true, "", load_titles_after_unlock(session)};
    };

    // Zeroize originals — copies moved into the lambda above.
    zeroize_string(values_[FIELD_PASSPHRASE]);
    zeroize_string(values_[FIELD_PASSWORD]);

    brls::Application::pushActivity(new LoadingActivity(session_, std::move(worker)));
}
