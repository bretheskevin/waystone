#include "update_flow.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"
#include "wizard.h"
#include "version.h"
#include "keymap_3ds.h"
#include <cstdio>

UpdateFlow::UpdateFlow()
    : worker_(new UpdateWorker()),
      phase_(UpdatePhase::Idle),
      last_phase_(UpdatePhase::Idle),
      confirm_update_(false),
      update_cancelled_(false),
      install_started_(false),
      restart_prompt_(false),
      status_changed_(false) {}

UpdateFlow::~UpdateFlow() {
    // App teardown never runs deferred reapers, so a still-owned worker is joined here.
    if (worker_) { worker_->request_cancel(); worker_->join(); delete worker_; }
}

void UpdateFlow::release() {
    if (worker_) printf("[update] flow: releasing worker (running=%d)\n", worker_->is_running() ? 1 : 0);
    reap_worker(worker_);
    confirm_update_ = false;
    restart_prompt_ = false;
}

void UpdateFlow::set_status(const std::string& s) {
    if (s == status_text_) return;
    status_text_ = s;
    status_changed_ = true;
}

void UpdateFlow::start_check(const char* origin) {
    if (!worker_) {
        printf("[update] check ignored -- worker released (%s)\n", origin);
        return;
    }
    if (worker_->is_running()) {
        printf("[update] check ignored -- update worker busy (%s)\n", origin);
        return;
    }
    printf("[update] check requested from %s (mode=%s)\n", origin,
           worker_->cia_mode() ? "cia" : "3dsx");
    install_started_ = false;
    confirm_update_ = false;
    restart_prompt_ = false;
    update_cancelled_ = false;
    pending_ver_.clear();
    pending_url_.clear();
    set_status(update_status_checking());
    phase_ = UpdatePhase::Checking;
    worker_->start_check();
    last_phase_ = UpdatePhase::Checking;  // a check that fails before the next poll still reports
}

bool UpdateFlow::poll() {
    if (worker_) {
        phase_ = worker_->phase();

        if (phase_ == UpdatePhase::Checking) {
            set_status(update_status_checking());
        } else if (phase_ == UpdatePhase::Downloading) {
            set_status(update_status_downloading(worker_->progress_percent()));
        } else if (phase_ == UpdatePhase::Installing) {
            set_status(update_status_installing(worker_->progress_percent()));
        }
        if (phase_ == UpdatePhase::Installing && last_phase_ != UpdatePhase::Installing) {
            printf("[update] ui: download done, installing CIA\n");
        }

        if (last_phase_ == UpdatePhase::Checking &&
            (phase_ == UpdatePhase::Done || phase_ == UpdatePhase::Error)) {
            std::string ver = worker_->latest_version();
            UpdateCheckOutcome o = update_check_outcome(worker_->check_rc(), ver.c_str(),
                                                        WS_APP_VERSION, worker_->asset_suffix());
            if (o.kind == UpdateCheckOutcome::Available) {
                pending_ver_ = ver;
                pending_url_ = worker_->asset_url();
                confirm_update_ = true;
                printf("[update] flow: offering v%s (%s)\n", ver.c_str(), worker_->asset_suffix());
            }
            set_status(o.status);
        }

        if (install_started_ &&
            (last_phase_ == UpdatePhase::Downloading || last_phase_ == UpdatePhase::Installing) &&
            (phase_ == UpdatePhase::Done || phase_ == UpdatePhase::Error)) {
            int rc = worker_->install_rc();
            if (rc == UP_OK && worker_->cia_mode()) {
                set_status(update_status_updated(pending_ver_.c_str()));
                restart_prompt_ = true;
                printf("[update] cia install succeeded -> restart prompt\n");
            } else if (rc == UP_OK) {
                set_status(update_status_restart_to_apply(pending_ver_.c_str()));
                printf("[update] install succeeded\n");
            } else {
                set_status(updater_install_message(rc));
                printf("[update] install failed rc=%d\n", rc);
            }
        }

        last_phase_ = phase_;
    }
    bool changed = status_changed_;
    status_changed_ = false;
    return changed;
}

bool UpdateFlow::handle_input(u32 kDown) {
    if (restart_prompt_) {
        if (kDown & ws_key(WsAction::Confirm)) {
            printf("[update] restart now: aptSetChainloaderToSelf + quit (v%s)\n",
                   pending_ver_.c_str());
            restart_prompt_ = false;
            set_status(update_status_restarting());
            aptSetChainloaderToSelf();
            App::instance().quit();  // normal teardown; aptExit performs the relaunch
        } else if (kDown & ws_key(WsAction::Cancel)) {
            printf("[update] restart later (v%s installed)\n", pending_ver_.c_str());
            restart_prompt_ = false;
            set_status(update_status_installed(pending_ver_.c_str()));
        }
        return true;
    }
    if (confirm_update_) {
        if (kDown & ws_key(WsAction::Update)) {
            if (!worker_) {
                printf("[update] confirm ignored -- worker released\n");
                confirm_update_ = false;
                return true;
            }
            printf("[update] confirmed: installing v%s\n", pending_ver_.c_str());
            confirm_update_ = false;
            update_cancelled_ = false;
            install_started_ = true;
            set_status(update_status_downloading(0));
            worker_->start_install(pending_url_);
            last_phase_ = UpdatePhase::Downloading;  // e.g. UP_NO_SELF lands before the next poll
        } else if (kDown & ws_key(WsAction::Cancel)) {
            printf("[update] update postponed (v%s)\n", pending_ver_.c_str());
            confirm_update_ = false;
            update_cancelled_ = true;
            set_status(update_status_postponed());
        }
        return true;
    }
    return false;
}

void UpdateFlow::poll_into(Wizard* wizard, bool muted) {
    if (poll() && !muted && !status_text_.empty()) wizard->set_status(status_text_);
}

bool UpdateFlow::handle_wizard_input(u32 kDown, Wizard* wizard, const char* origin,
                                     bool check_allowed) {
    if (handle_input(kDown)) return true;
    if (!check_allowed || !(kDown & ws_key(WsAction::CheckUpdates))) return false;
    wizard->clear_error();
    start_check(origin);
    return true;
}

bool UpdateFlow::draw_banner(C2D_TextBuf buf) const {
    if (!modal()) return false;
    std::string title, detail, hint;
    if (restart_prompt_) {
        title = update_restart_title();
        detail = update_restart_detail();
        hint = ws_confirm_cancel_hint(update_restart_now_label(), update_restart_later_label());
    } else {
        static const WsAction update_acts[] = { WsAction::Update, WsAction::Cancel };
        title = update_offer_title();
        detail = update_offer_detail(pending_ver_.c_str());
        hint = ws_hint_bar(update_acts);
    }
    draw_confirm_banner(buf, (float)SP_MD, 150.0f, title.c_str(), detail.c_str(), hint.c_str(), 56.0f);
    draw_footer_hint(buf, hint.c_str());
    return true;
}

u32 UpdateFlow::status_color() const {
    if (phase_ == UpdatePhase::Error) return CLR_ERROR;
    if (phase_ == UpdatePhase::Done && !confirm_update_ && !update_cancelled_) return CLR_SUCCESS;
    if (phase_ == UpdatePhase::Checking || phase_ == UpdatePhase::Downloading ||
        phase_ == UpdatePhase::Installing)
        return CLR_SYNC;
    return CLR_NEUTRAL_400;
}
