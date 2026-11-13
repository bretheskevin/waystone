#include "unlock_screen.h"
#include "title_list_screen.h"
#include "setup_screen.h"
#include "loading_screen.h"
#include "session_store.h"
#include "app.h"
#include "theme.h"
#include "worker_thread.h"
#include "vault_helpers.h"
#include "ffi_err.h"
#include "keys_file.h"
#include "vault_remote.h"
#include "setup_model.h"
#include "widgets.h"
#include "keymap_3ds.h"
#include <cstdio>

struct UnlockWorkerCtx {
    UnlockScreen* self;
    std::string   passphrase;
    bool          recovery_mode;
};

// plain_ and recovery_mode_ must be declared before wizard_ so they are initialized first
// (C++ initializes members in declaration order, not initializer-list order).

UnlockScreen::UnlockScreen(Session* session, uint8_t* keys_data, size_t keys_len)
    : session_(session), keys_data_(keys_data), keys_len_(keys_len),
      plain_(ffi_keys_plain(keys_data, keys_len)),
      recovery_mode_(false),
      wizard_(make_steps(), NUM_VALUES),
      unlocking_(false), unlock_done_(false), unlock_thread_(NULL),
      unlock_success_(false), forget_confirm_(false) {
    wizard_.set_extra_hint_actions(unlock_extra_actions());
    printf("[unlock] screen created (mode=%s)\n", keys_mode_name(plain_));
}

UnlockScreen::~UnlockScreen() {
    if (unlocking_) {
        threadJoin(unlock_thread_, U64_MAX);
        threadFree(unlock_thread_);
    }
    wizard_.zeroize_secrets();
}

std::vector<WizardStepDef> UnlockScreen::make_steps() const {
    WizardFieldDef pwd_field;
    pwd_field.value_index = FIELD_PASSWORD;
    pwd_field.label       = "WebDAV Password";
    pwd_field.is_secret   = true;
    pwd_field.placeholder = "not set";
    pwd_field.hint        = unlock_password_hint(plain_);

    WizardStepDef step;
    step.title = unlock_title(plain_);
    if (plain_) {
        step.hint = pwd_field.hint;
    } else {
        WizardFieldDef pass_field = vault_secret_field(FIELD_PASSPHRASE, recovery_mode_);
        step.hint = pass_field.hint;
        step.fields.push_back(pass_field);
    }
    step.fields.push_back(pwd_field);

    std::vector<WizardStepDef> steps;
    steps.push_back(step);
    return steps;
}

bool UnlockScreen::validate() {
    wizard_.clear_error();
    if (!plain_ && wizard_.value(FIELD_PASSPHRASE).empty()) {
        wizard_.set_error(vault_secret_required(recovery_mode_));
        return false;
    }
    if (wizard_.value(FIELD_PASSWORD).empty()) {
        wizard_.set_error("WebDAV password is required");
        return false;
    }
    return true;
}

void UnlockScreen::unlock_thread_entry(void* arg) {
    UnlockWorkerCtx* ctx = static_cast<UnlockWorkerCtx*>(arg);
    UnlockScreen* self = ctx->self;

    bool plain = self->plain_;
    printf("[unlock] worker: ws_vault_open (mode=%s, recovery=%d)\n",
           keys_mode_name(plain), ctx->recovery_mode ? 1 : 0);
    WsVault* vault = ws_vault_open(self->keys_data_, self->keys_len_,
                                   plain ? NULL : ctx->passphrase.c_str(), ctx->recovery_mode);
    zeroize_string(ctx->passphrase);

    const char* kind = plain ? "plain login" : "unlock";
    if (vault) {
        printf("[unlock] worker: %s ok\n", kind);
        self->session_->vault  = vault;
        self->unlock_success_  = true;
    } else {
        const char* err = ws_last_error();
        printf("[unlock] worker: %s FAILED: %s\n", kind, ffi_err());
        self->unlock_error_   = unlock_failed_message(plain, err);
        self->unlock_success_ = false;
    }
    self->unlock_done_.store(true);
    delete ctx;
}

void UnlockScreen::do_unlock() {
    if (unlocking_) return;
    if (!validate()) return;

    unlocking_ = true;
    wizard_.set_status("Unlocking... please wait");
    unlock_done_.store(false);
    unlock_success_ = false;

    UnlockWorkerCtx* ctx = new UnlockWorkerCtx();
    ctx->self          = this;
    if (!plain_) ctx->passphrase = wizard_.value(FIELD_PASSPHRASE);
    ctx->recovery_mode = !plain_ && recovery_mode_;

    printf("[unlock] do_unlock: starting worker thread\n");
    unlock_thread_ = start_worker_thread(unlock_thread_entry, ctx);
    if (!unlock_thread_) {
        zeroize_string(ctx->passphrase);
        delete ctx;
        unlocking_ = false;
        wizard_.set_error("Failed to create worker thread");
        wizard_.set_status("");
    }
}

void UnlockScreen::draw_top(C3D_RenderTarget* target) {
    wizard_.draw_top(target, App::instance().text_buf(), unlock_title(plain_));
}

void UnlockScreen::draw_bottom(C3D_RenderTarget* target) {
    C2D_TextBuf buf = App::instance().text_buf();
    if (!unlocking_ && update_.draw_banner(buf)) return;  // input is ignored while unlocking
    if (forget_confirm_) {
        std::string hint = ws_confirm_cancel_hint(forget_server_confirm_label());
        draw_confirm_banner(buf, (float)SP_MD, 150.0f, forget_server_title(), forget_server_detail(),
                            0, 64.0f);
        draw_footer_hint(buf, hint.c_str());
        return;
    }
    wizard_.draw_bottom(target, buf, unlock_finish_label(plain_));
}

void UnlockScreen::forget_server() {
    printf("[ui] unlock: forget confirmed\n");
    bool ok = vault_forget_local(WS_KEYS_PATH, ws_recovery_path(session_->device_id),
                                 session_->vault, session_->dav.user, session_->dav.pass);
    printf("[ui] unlock: local login removed %s (config kept)\n", ok ? "ok" : "with errors");
    wizard_.zeroize_secrets();
    update_.release();
    Session* session = session_;
    printf("[ui] unlock: forget done -> SetupScreen\n");
    // set_screen deletes 'this' -- no member access after this point
    App::instance().set_screen(new SetupScreen(session));
}

void UnlockScreen::handle_input(u32 kDown, touchPosition touch) {
    if (unlocking_) return;
    if (update_.handle_wizard_input(kDown, &wizard_, "unlock", !forget_confirm_)) return;
    if (forget_confirm_) {
        if (kDown & ws_key(WsAction::Confirm)) {
            forget_confirm_ = false;
            forget_server();  // deletes this screen
            return;
        }
        if (kDown & ws_key(WsAction::Cancel)) {
            printf("[ui] unlock: forget cancelled\n");
            forget_confirm_ = false;
        }
        return;
    }
    if (kDown & ws_key(WsAction::ForgetServer)) {
        if (update_.busy()) {
            printf("[ui] unlock: forget ignored: update in progress\n");
            return;
        }
        printf("[ui] unlock: forget requested\n");
        wizard_.clear_error();
        forget_confirm_ = true;
        return;
    }
    if (!plain_ &&
        vault_secret_toggle(kDown, &recovery_mode_, &wizard_, FIELD_PASSPHRASE,
                            [this] { return make_steps(); }))
        return;
    int result = wizard_.handle_input(kDown, touch);
    if (result == 2 || result == 3) {
        do_unlock();
    }
}

void UnlockScreen::poll() {
    update_.poll_into(&wizard_, unlocking_);
    if (!unlocking_ || !unlock_done_.load()) return;

    threadJoin(unlock_thread_, U64_MAX);
    threadFree(unlock_thread_);
    unlock_thread_ = NULL;
    unlocking_ = false;
    wizard_.set_status("");

    if (!unlock_success_) {
        wizard_.set_error(unlock_error_);
        unlock_error_.clear();
        return;
    }

    session_->dav.server_url = session_->config.server_url;
    session_->dav.user       = session_->config.username;
    session_->dav.pass       = wizard_.value(FIELD_PASSWORD);
    // Persist the session for auto-unlock on next launch
    persist_session(session_->vault, session_->dav.pass);
    wizard_.zeroize_secrets();
    update_.release();

    // set_screen deletes 'this' — no member access after this point
    // list_titles runs off the render thread inside LoadingScreen
    App::instance().set_screen(new LoadingScreen(session_, false));
}
