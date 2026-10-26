#include "setup_screen.h"
#include "recovery_screen.h"
#include "loading_screen.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"
#include "worker_thread.h"
#include "session_store.h"
#include <cstdio>
#include <cstring>
#include <utility>

extern "C" {
struct Vault;
#include "waystone.h"
}

std::vector<WizardStepDef> SetupScreen::make_steps() const {
    std::vector<WizardStepDef> steps;

    WizardStepDef welcome;
    welcome.title = "Welcome to Waystone";
    welcome.hint  = "Press (R) to begin setup";
    steps.push_back(welcome);

    WizardStepDef server;
    server.title = "Server URL";
    WizardFieldDef sf; sf.value_index=FIELD_SERVER; sf.label="Server URL"; sf.is_secret=false;
    sf.placeholder="not set"; sf.hint="your Waystone server, e.g. https://dav.example.com";
    server.fields.push_back(sf);
    steps.push_back(server);

    WizardStepDef login;
    login.title = "WebDAV Login";
    WizardFieldDef uf; uf.value_index=FIELD_USERNAME; uf.label="WebDAV Username"; uf.is_secret=false;
    uf.placeholder="not set"; uf.hint="your WebDAV username";
    WizardFieldDef pf; pf.value_index=FIELD_PASSWORD; pf.label="WebDAV Password"; pf.is_secret=true;
    pf.placeholder="not set"; pf.hint="your WebDAV password (stored in RAM only)";
    login.fields.push_back(uf);
    login.fields.push_back(pf);
    steps.push_back(login);

    WizardStepDef vault;
    vault.title = setup_vault_step_title(vault_mode());
    if (joining_) {
        vault.fields.push_back(vault_secret_field(FIELD_PASSPHRASE, recovery_));
    } else {
        WizardFieldDef ppf; ppf.value_index=FIELD_PASSPHRASE; ppf.label="Passphrase"; ppf.is_secret=true;
        ppf.placeholder="not set"; ppf.hint="choose a strong passphrase to encrypt your saves";
        WizardFieldDef cpf; cpf.value_index=FIELD_CONFIRM; cpf.label="Confirm Passphrase"; cpf.is_secret=true;
        cpf.placeholder="not set"; cpf.hint="re-enter your vault passphrase";
        vault.fields.push_back(ppf);
        vault.fields.push_back(cpf);
    }
    steps.push_back(vault);

    return steps;
}

struct SetupJobCtx {
    SetupScreen*     self;
    SetupScreen::Job job;
    OwnedWebDavCfg   dav;
    std::string      secret;
    bool             recovery;
    std::vector<uint8_t> server_keys;
    std::string      recovery_path;
    SetupJobCtx() : self(NULL), job(SetupScreen::JOB_NONE), recovery(false) {}
};

SetupScreen::SetupScreen(Session* session)
    : session_(session), joining_(false), recovery_(false),
      wizard_(make_steps(), NUM_VALUES),
      job_(JOB_NONE), job_done_(false), job_thread_(NULL), spinner_angle_(0.0f) {
    probe_.state = RemoteKeysState::Error;
    probe_.http_status = -1;
    result_.vault = NULL;
}

SetupScreen::~SetupScreen() {
    if (job_ != JOB_NONE) {
        threadJoin(job_thread_, U64_MAX);
        threadFree(job_thread_);
        job_ = JOB_NONE;
    }
    vault_setup_wipe_recovery(&result_);
    if (result_.vault) {
        ws_vault_free(result_.vault);
        result_.vault = NULL;
    }
    wizard_.zeroize_secrets();
}

void SetupScreen::draw_top(C3D_RenderTarget* target) {
    wizard_.draw_top(target, App::instance().text_buf(), "Setup");
    if (job_ != JOB_NONE)
        draw_spinner(App::instance().text_buf(), (float)SCREEN_TOP_W / 2.0f, 130.0f, spinner_angle_);
}

void SetupScreen::draw_bottom(C3D_RenderTarget* target) {
    const char* finish_label = NULL;
    if (wizard_.is_last_step())
        finish_label = setup_finish_label(vault_mode(), job_ == JOB_JOIN || job_ == JOB_CREATE);
    wizard_.draw_bottom(target, App::instance().text_buf(), finish_label);
}

void SetupScreen::set_vault_mode(bool joining) {
    if (joining == joining_) return;
    joining_  = joining;
    recovery_ = false;
    zeroize_string(wizard_.value(FIELD_PASSPHRASE));
    zeroize_string(wizard_.value(FIELD_CONFIRM));
    wizard_.reload_steps(make_steps());
}

bool SetupScreen::validate_step(size_t step) {
    wizard_.clear_error();
    switch (step) {
        case STEP_SERVER:
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
        case STEP_LOGIN:
            if (wizard_.value(FIELD_USERNAME).empty()) {
                wizard_.set_error("WebDAV Username is required");
                return false;
            }
            if (wizard_.value(FIELD_PASSWORD).empty()) {
                wizard_.set_error("WebDAV Password is required");
                return false;
            }
            return true;
        case STEP_VAULT:
            if (wizard_.value(FIELD_PASSPHRASE).empty()) {
                wizard_.set_error(vault_secret_required(joining_ && recovery_));
                return false;
            }
            if (joining_) return true;
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

void SetupScreen::job_thread_entry(void* arg) {
    SetupJobCtx* ctx = static_cast<SetupJobCtx*>(arg);
    SetupScreen* self = ctx->self;
    WebDavCfg cfg = ctx->dav.as_cfg();
    if (ctx->job == JOB_PROBE) {
        printf("[vault] setup worker: probing server for /keys.json\n");
        self->probe_ = vault_fetch_remote_keys(cfg);
    } else {
        const char* what = ctx->job == JOB_CREATE ? "create" : "join";
        printf("[vault] setup worker: %s start\n", what);
        VaultSetupResult res = ctx->job == JOB_CREATE
            ? vault_create_and_publish(cfg, ctx->secret)
            : vault_join(ctx->server_keys, ctx->secret, ctx->recovery);
        zeroize_string(ctx->secret);
        if (res.vault) {
            vault_setup_persist(res, WS_KEYS_PATH, ctx->recovery_path, self->session_->config,
                                self->session_->config_path.c_str());
            printf("[vault] setup worker: %s succeeded\n", what);
        } else {
            printf("[vault] setup worker: %s FAILED: %s\n", what, res.error.c_str());
        }
        std::swap(self->result_, res);
        vault_setup_wipe_recovery(&res);
    }
    self->job_done_.store(true);
    delete ctx;
}

void SetupScreen::start_job(Job job) {
    if (job_ != JOB_NONE) return;
    SetupJobCtx* ctx = new SetupJobCtx();
    ctx->self           = this;
    ctx->job            = job;
    ctx->dav.server_url = wizard_.value(FIELD_SERVER);
    ctx->dav.user       = wizard_.value(FIELD_USERNAME);
    ctx->dav.pass       = wizard_.value(FIELD_PASSWORD);

    if (job == JOB_PROBE) {
        printf("[ui] setup: checking server for an existing vault\n");
        wizard_.set_status(setup_probe_status());
    } else {
        session_->config.server_url = wizard_.value(FIELD_SERVER);
        session_->config.username   = wizard_.value(FIELD_USERNAME);
        ctx->secret = wizard_.value(FIELD_PASSPHRASE);
        if (job == JOB_JOIN) {
            printf("[ui] setup: joining vault (%s)\n", recovery_ ? "recovery key" : "passphrase");
            ctx->server_keys = server_keys_;
            ctx->recovery    = recovery_;
            wizard_.set_status(setup_work_status(SetupVaultMode::Join));
        } else {
            printf("[ui] setup: creating vault\n");
            recovery_path_     = ws_recovery_path(session_->device_id);
            ctx->recovery_path = recovery_path_;
            wizard_.set_status(setup_work_status(SetupVaultMode::Create));
        }
    }

    job_done_.store(false);
    job_ = job;
    job_thread_ = start_worker_thread(job_thread_entry, ctx);
    if (!job_thread_) {
        zeroize_string(ctx->secret);
        delete ctx;
        job_ = JOB_NONE;
        wizard_.set_error("Failed to create worker thread");
        wizard_.set_status("");
    }
}

void SetupScreen::handle_input(u32 kDown, touchPosition touch) {
    if (job_ != JOB_NONE) return;
    if (joining_ && wizard_.current_step() == STEP_VAULT &&
        vault_secret_toggle(kDown, &recovery_, &wizard_, FIELD_PASSPHRASE,
                            [this] { return make_steps(); }))
        return;
    int result = wizard_.handle_input(kDown, touch);
    if (result == 2 || (result == 3 && wizard_.is_last_step())) {
        if (validate_step(wizard_.current_step())) start_job(joining_ ? JOB_JOIN : JOB_CREATE);
    } else if (result == 3) {
        if (!validate_step(wizard_.current_step())) return;
        if (wizard_.current_step() == STEP_LOGIN) start_job(JOB_PROBE);
        else wizard_.go_next();
    } else if (result == 4) {
        wizard_.go_back();
    }
}

void SetupScreen::on_probe_done() {
    switch (probe_.state) {
        case RemoteKeysState::Present:
            printf("[ui] setup: server has a vault -> join step\n");
            server_keys_.swap(probe_.bytes);
            set_vault_mode(true);
            wizard_.go_next();
            break;
        case RemoteKeysState::Absent:
            printf("[ui] setup: no vault on server -> create step\n");
            server_keys_.clear();
            set_vault_mode(false);
            wizard_.go_next();
            break;
        case RemoteKeysState::Error:
            printf("[ui] setup: server probe failed (http=%d)\n", probe_.http_status);
            wizard_.set_error(setup_probe_error(probe_.http_status));
            break;
    }
    probe_.bytes.clear();
}

void SetupScreen::on_vault_done(Job job) {
    WsVault* vault = result_.vault;
    result_.vault  = NULL;
    if (!vault) {
        printf("[ui] setup: %s failed -- showing error\n", job == JOB_JOIN ? "join" : "create");
        wizard_.set_error(result_.error);
        result_.error.clear();
        return;
    }

    session_->vault          = vault;
    session_->dav.server_url = wizard_.value(FIELD_SERVER);
    session_->dav.user       = wizard_.value(FIELD_USERNAME);
    session_->dav.pass       = wizard_.value(FIELD_PASSWORD);
    bool persisted = persist_session(session_->vault, session_->dav.pass);
    printf("[vault] setup: session persist %s\n", persisted ? "ok" : "FAILED");
    wizard_.zeroize_secrets();

    if (job == JOB_JOIN) {
        printf("[ui] setup: joined vault -> loading titles\n");
        // set_screen deletes 'this' — no member access after this point
        App::instance().set_screen(new LoadingScreen(session_, false));
        return;
    }

    printf("[ui] setup: vault created -> recovery screen\n");
    std::string recovery_hex  = result_.recovery_hex;
    vault_setup_wipe_recovery(&result_);
    std::string recovery_path = recovery_path_;
    Session* session          = session_;
    // set_screen deletes 'this' — no member access after this point
    App::instance().set_screen(new RecoveryScreen(session, recovery_hex, recovery_path));
    zeroize_string(recovery_hex);
}

void SetupScreen::poll() {
    if (job_ != JOB_NONE) spinner_angle_ += 0.05f;
    if (job_ == JOB_NONE || !job_done_.load()) return;

    threadJoin(job_thread_, U64_MAX);
    threadFree(job_thread_);
    job_thread_ = NULL;
    Job job = job_;
    job_ = JOB_NONE;
    wizard_.set_status("");

    if (job == JOB_PROBE) on_probe_done();
    else on_vault_done(job);
}
