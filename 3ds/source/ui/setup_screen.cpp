#include "setup_screen.h"
#include "recovery_screen.h"
#include "app.h"
#include "theme.h"
#include "worker_thread.h"
#include "session_store.h"
#include <cstdio>
#include <cstring>

extern "C" {
struct Vault;
#include "waystone.h"
}

static std::vector<WizardStepDef> setup_steps() {
    std::vector<WizardStepDef> steps;

    WizardStepDef welcome;
    welcome.title = "Welcome to Waystone";
    welcome.hint  = "Press (R) to begin setup";
    steps.push_back(welcome);

    WizardStepDef server;
    server.title = "Server URL";
    WizardFieldDef sf; sf.value_index=0; sf.label="Server URL"; sf.is_secret=false;
    sf.placeholder="not set"; sf.hint="your Waystone server, e.g. https://dav.example.com";
    server.fields.push_back(sf);
    steps.push_back(server);

    WizardStepDef login;
    login.title = "WebDAV Login";
    WizardFieldDef uf; uf.value_index=1; uf.label="WebDAV Username"; uf.is_secret=false;
    uf.placeholder="not set"; uf.hint="your WebDAV username";
    WizardFieldDef pf; pf.value_index=2; pf.label="WebDAV Password"; pf.is_secret=true;
    pf.placeholder="not set"; pf.hint="your WebDAV password (stored in RAM only)";
    login.fields.push_back(uf);
    login.fields.push_back(pf);
    steps.push_back(login);

    WizardStepDef phrase;
    phrase.title = "Vault Passphrase";
    WizardFieldDef ppf; ppf.value_index=3; ppf.label="Passphrase"; ppf.is_secret=true;
    ppf.placeholder="not set"; ppf.hint="choose a strong passphrase to encrypt your saves";
    WizardFieldDef cpf; cpf.value_index=4; cpf.label="Confirm Passphrase"; cpf.is_secret=true;
    cpf.placeholder="not set"; cpf.hint="re-enter your vault passphrase";
    phrase.fields.push_back(ppf);
    phrase.fields.push_back(cpf);
    steps.push_back(phrase);

    return steps;
}

struct VaultWorkerCtx {
    SetupScreen* self;
    std::string  passphrase;
};

SetupScreen::SetupScreen(Session* session)
    : session_(session), wizard_(setup_steps(), NUM_VALUES),
      creating_(false), vault_done_(false), vault_thread_(NULL) {}

SetupScreen::~SetupScreen() {
    if (creating_) {
        threadJoin(vault_thread_, U64_MAX);
        threadFree(vault_thread_);
        creating_ = false;
    }
    zeroize_string(vault_result_.recovery_hex);
    if (vault_result_.vault) {
        ws_vault_free(vault_result_.vault);
        vault_result_.vault = NULL;
    }
    wizard_.zeroize_secrets();
}

void SetupScreen::draw_top(C3D_RenderTarget* target) {
    wizard_.draw_top(target, App::instance().text_buf(), "Waystone Setup");
}

void SetupScreen::draw_bottom(C3D_RenderTarget* target) {
    const char* finish_label = NULL;
    if (wizard_.is_last_step()) {
        finish_label = creating_ ? "Creating vault..." : "Create Vault";
    }
    wizard_.draw_bottom(target, App::instance().text_buf(), finish_label);
}

bool SetupScreen::validate_step(size_t step) {
    wizard_.clear_error();
    switch (step) {
        case 1:
            if (wizard_.value(FIELD_SERVER).empty()) {
                wizard_.set_error("Server URL is required");
                return false;
            }
            if (wizard_.value(FIELD_SERVER).rfind("http://", 0) != 0 &&
                wizard_.value(FIELD_SERVER).rfind("https://", 0) != 0) {
                wizard_.set_error("Server URL must start with http:// or https://");
                return false;
            }
            return true;
        case 2:
            if (wizard_.value(FIELD_USERNAME).empty()) {
                wizard_.set_error("WebDAV Username is required");
                return false;
            }
            if (wizard_.value(FIELD_PASSWORD).empty()) {
                wizard_.set_error("WebDAV Password is required");
                return false;
            }
            return true;
        case 3:
            if (wizard_.value(FIELD_PASSPHRASE).empty()) {
                wizard_.set_error("Passphrase is required");
                return false;
            }
            if (wizard_.value(FIELD_CONFIRM).empty()) {
                wizard_.set_error("Please confirm your passphrase");
                return false;
            }
            if (wizard_.value(FIELD_CONFIRM) != wizard_.value(FIELD_PASSPHRASE)) {
                wizard_.set_error("Passphrases do not match");
                zeroize_string(wizard_.value(FIELD_CONFIRM));
                return false;
            }
            return true;
        default:
            return true;
    }
}

void SetupScreen::on_finish() {
    if (!validate_step(wizard_.current_step())) return;
    do_create_vault();
}

void SetupScreen::vault_thread_entry(void* arg) {
    VaultWorkerCtx* ctx = static_cast<VaultWorkerCtx*>(arg);
    SetupScreen* self = ctx->self;
    printf("[vault] worker: calling create_vault\n");
    VaultCreateResult res = create_vault(ctx->passphrase, self->session_);
    zeroize_string(ctx->passphrase);
    if (res.vault) {
        printf("[vault] worker: create_vault succeeded\n");
    } else {
        printf("[vault] worker: create_vault FAILED: %s\n", res.error.c_str());
    }
    self->vault_result_ = res;
    res.vault = NULL;  // ownership transferred; prevents any future double-free via res
    zeroize_string(res.recovery_hex);
    self->vault_done_.store(true);
    delete ctx;
}

void SetupScreen::do_create_vault() {
    if (creating_) return;
    printf("[vault] do_create_vault: entered\n");
    session_->config.server_url = wizard_.value(FIELD_SERVER);
    session_->config.username   = wizard_.value(FIELD_USERNAME);
    creating_ = true;
    wizard_.set_status("Creating vault... please wait");
    vault_done_.store(false);

    VaultWorkerCtx* ctx = new VaultWorkerCtx();
    ctx->self       = this;
    ctx->passphrase = wizard_.value(FIELD_PASSPHRASE);

    printf("[vault] do_create_vault: starting worker thread\n");
    vault_thread_ = start_worker_thread(vault_thread_entry, ctx);
    if (!vault_thread_) {
        zeroize_string(ctx->passphrase);
        delete ctx;
        creating_ = false;
        wizard_.set_error("Failed to create worker thread");
        wizard_.set_status("");
    }
}

void SetupScreen::handle_input(u32 kDown, touchPosition touch) {
    if (creating_) return;
    int result = wizard_.handle_input(kDown, touch);
    switch (result) {
        case 2:
            on_finish();
            break;
        case 3:
            if (wizard_.is_last_step()) {
                on_finish();
            } else if (validate_step(wizard_.current_step())) {
                wizard_.go_next();
            }
            break;
        case 4:
            wizard_.go_back();
            break;
        default:
            break;
    }
}

void SetupScreen::poll() {
    if (!creating_ || !vault_done_.load()) return;

    threadJoin(vault_thread_, U64_MAX);
    threadFree(vault_thread_);
    vault_thread_ = NULL;
    creating_ = false;
    wizard_.set_status("");

    WsVault*    vault_ptr     = vault_result_.vault;
    vault_result_.vault       = NULL;
    std::string recovery_hex  = vault_result_.recovery_hex;
    zeroize_string(vault_result_.recovery_hex);
    std::string recovery_path = vault_result_.recovery_path;
    std::string err_msg       = vault_result_.error;

    if (!vault_ptr) {
        printf("[vault] poll: failure -- showing error\n");
        wizard_.set_error(err_msg);
        return;
    }

    printf("[vault] poll: success -- setting up session\n");
    session_->vault          = vault_ptr;
    session_->dav.server_url = wizard_.value(FIELD_SERVER);
    session_->dav.user       = wizard_.value(FIELD_USERNAME);
    session_->dav.pass       = wizard_.value(FIELD_PASSWORD);
    // Persist the session for auto-unlock on next launch
    persist_session(session_->vault, session_->dav.pass);
    wizard_.zeroize_secrets();

    // set_screen deletes 'this' — no member access after this point
    App::instance().set_screen(new RecoveryScreen(session_, recovery_hex, recovery_path));
    zeroize_string(recovery_hex);  // local var on stack, safe after 'this' deleted
}
