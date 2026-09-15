#include "unlock_screen.h"
#include "title_list_screen.h"
#include "loading_screen.h"
#include "session_store.h"
#include "app.h"
#include "theme.h"
#include "worker_thread.h"
#include <cstdio>

extern "C" {
struct Vault;
#include "waystone.h"
}

struct UnlockWorkerCtx {
    UnlockScreen* self;
    std::string   passphrase;
    bool          recovery_mode;
};

// recovery_mode_ must be declared before wizard_ so it is initialized first
// (C++ initializes members in declaration order, not initializer-list order).

UnlockScreen::UnlockScreen(Session* session, uint8_t* keys_data, size_t keys_len)
    : session_(session), keys_data_(keys_data), keys_len_(keys_len),
      recovery_mode_(false),
      wizard_(make_steps(), NUM_VALUES),
      unlocking_(false), unlock_done_(false), unlock_thread_(NULL),
      unlock_success_(false) {}

UnlockScreen::~UnlockScreen() {
    if (unlocking_) {
        threadJoin(unlock_thread_, U64_MAX);
        threadFree(unlock_thread_);
    }
    wizard_.zeroize_secrets();
}

std::vector<WizardStepDef> UnlockScreen::make_steps() const {
    WizardFieldDef pass_field;
    if (recovery_mode_) {
        pass_field.value_index = FIELD_PASSPHRASE;
        pass_field.label       = "Recovery Key";
        pass_field.is_secret   = false;
        pass_field.placeholder = "not set";
        pass_field.hint        = "enter your hex recovery key (Y to switch to passphrase)";
    } else {
        pass_field.value_index = FIELD_PASSPHRASE;
        pass_field.label       = "Vault Passphrase";
        pass_field.is_secret   = true;
        pass_field.placeholder = "not set";
        pass_field.hint        = "enter your vault passphrase (Y to switch to recovery key)";
    }
    WizardFieldDef pwd_field;
    pwd_field.value_index = FIELD_PASSWORD;
    pwd_field.label       = "WebDAV Password";
    pwd_field.is_secret   = true;
    pwd_field.placeholder = "not set";
    pwd_field.hint        = "your WebDAV password";

    WizardStepDef step;
    step.title = "Unlock Vault";
    step.fields.push_back(pass_field);
    step.fields.push_back(pwd_field);

    std::vector<WizardStepDef> steps;
    steps.push_back(step);
    return steps;
}

bool UnlockScreen::validate() {
    wizard_.clear_error();
    if (wizard_.value(FIELD_PASSPHRASE).empty()) {
        wizard_.set_error(recovery_mode_ ? "Recovery key is required" : "Passphrase is required");
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

    printf("[unlock] worker: calling ws_vault_unlock\n");
    WsVault* vault = NULL;
    if (ctx->recovery_mode) {
        vault = ws_vault_unlock_recovery(ctx->passphrase.c_str(),
                                         self->keys_data_, self->keys_len_);
    } else {
        vault = ws_vault_unlock_pass(ctx->passphrase.c_str(),
                                     self->keys_data_, self->keys_len_);
    }
    zeroize_string(ctx->passphrase);

    if (vault) {
        printf("[unlock] worker: unlock succeeded\n");
        self->session_->vault  = vault;
        self->unlock_success_  = true;
    } else {
        const char* err = ws_last_error();
        printf("[unlock] worker: unlock FAILED: %s\n", err ? err : "unknown");
        self->unlock_error_   = std::string("Unlock failed: ") + (err ? err : "wrong passphrase or key");
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
    ctx->passphrase    = wizard_.value(FIELD_PASSPHRASE);
    ctx->recovery_mode = recovery_mode_;

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
    wizard_.draw_top(target, App::instance().text_buf(), "Unlock Vault");
}

void UnlockScreen::draw_bottom(C3D_RenderTarget* target) {
    wizard_.draw_bottom(target, App::instance().text_buf(), "Unlock");
}

void UnlockScreen::handle_input(u32 kDown, touchPosition touch) {
    if (unlocking_) return;
    if (kDown & KEY_Y) {
        recovery_mode_ = !recovery_mode_;
        zeroize_string(wizard_.value(FIELD_PASSPHRASE));
        wizard_.clear_error();
        wizard_.reload_steps(make_steps());
        return;
    }
    int result = wizard_.handle_input(kDown, touch);
    if (result == 2 || result == 3) {
        do_unlock();
    }
}

void UnlockScreen::poll() {
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

    // set_screen deletes 'this' — no member access after this point
    // list_titles runs off the render thread inside LoadingScreen
    App::instance().set_screen(new LoadingScreen(session_, false));
}
