#include "unlock_activity.h"
#include "loading_activity.h"
#include "vault_helpers.h"
#include "session_store.h"
#include "ffi_err.h"
#include <cstdio>

UnlockActivity::UnlockActivity(Session* session, const uint8_t* keys_data, size_t keys_len)
    : WizardActivity(NUM_VALUES), session_(session), keys_data_(keys_data), keys_len_(keys_len),
      plain_(ffi_keys_plain(keys_data, keys_len)) {
    printf("[unlock] local keys mode=%s (len=%zu)\n", keys_mode_name(plain_), keys_len);
}

std::vector<WizardStepDef> UnlockActivity::get_steps() const {
    WizardFieldDef password_field = {FIELD_PASSWORD, "WebDAV Password", true, "not set",
                                     unlock_password_hint(plain_)};
    if (plain_) return {{unlock_title(true), "", {password_field}}};
    return {{unlock_title(false), "",
             {vault_secret_field(FIELD_PASSPHRASE, recovery_mode_), password_field}}};
}

std::string UnlockActivity::finish_label() const { return unlock_finish_label(plain_); }

std::string UnlockActivity::wizard_title() const { return unlock_title(plain_); }

bool UnlockActivity::validate_step(size_t /*step*/) {
    error_.clear();
    if (!plain_ && values_[FIELD_PASSPHRASE].empty()) {
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
    if (plain_) return;
    register_recovery_toggle(&recovery_mode_, FIELD_PASSPHRASE,
                             [this] { return current_step_ == 0; });
}

// Task 5 + 7b: run Argon2 KDF + title listing off the render thread; persist session.
void UnlockActivity::do_unlock() {
    std::string passphrase_copy = values_[FIELD_PASSPHRASE];
    std::string password_copy   = values_[FIELD_PASSWORD];
    bool recovery = recovery_mode_;
    bool plain    = plain_;
    std::vector<uint8_t> keys_copy(keys_data_, keys_data_ + keys_len_);
    Session* session = session_;

    auto worker = [session, pass = std::move(passphrase_copy),
                   pwd = std::move(password_copy),
                   keys = std::move(keys_copy), recovery, plain]() mutable
        -> LoadingActivity::LoadResult {
        const char* kind = plain ? "plain login" : (recovery ? "recovery unlock" : "unlock");
        printf("[unlock] %s start\n", kind);
        WsVault* vault = ws_vault_open(keys.data(), keys.size(), plain ? nullptr : pass.c_str(),
                                       !plain && recovery);
        zeroize_string(pass);

        if (!vault) {
            const char* err = ws_last_error();
            printf("[unlock] %s failed (err=%s)\n", kind, ffi_err());
            std::string msg = unlock_failed_message(plain, err);
            zeroize_string(pwd);
            return {false, std::move(msg), {}};
        }
        printf("[unlock] %s ok\n", kind);

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
