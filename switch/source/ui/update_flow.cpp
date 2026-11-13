#include "update_flow.h"
#include "worker_reaper.h"
#include "version.h"
#include "updater.h"
#include "kit/confirm_dialog.h"
#include "kit/overlay.h"
#include <borealis.hpp>
#include <switch.h>
#include <cstdio>

static const char* const NRO_SUFFIX = ".nro";

UpdateFlow::UpdateFlow(std::function<void(const std::string&)> status_sink, const char* origin)
    : sink_(std::move(status_sink)), origin_(origin), updater_(new UpdateController()) {
    printf("[update] flow created (%s)\n", origin_);
}

UpdateFlow::~UpdateFlow() {
    *alive_ = false;
    reap_worker(updater_);
    printf("[update] flow destroyed (%s)\n", origin_);
}

bool UpdateFlow::busy() const {
    return (updater_ && updater_->is_running()) || install_started_ || dialog_open_ ||
           offer_ != Offer::None;
}

void UpdateFlow::set_status(const std::string& text) {
    if (sink_) sink_(text);
}

void UpdateFlow::start_check() {
    if (!updater_ || busy()) {
        printf("[update] check ignored (%s): busy\n", origin_);
        return;
    }
    printf("[update] check requested from %s\n", origin_);
    install_started_ = false;
    updater_->start_check();
    last_phase_ = UpdatePhase::Checking;  // a check that fails before the next tick still reports
    set_status(update_status_checking());
}

void UpdateFlow::offer_install() {
    dialog_open_ = true;
    printf("[update] offering install of v%s (%s)\n", pending_ver_.c_str(), origin_);
    OverlayActivity::push(new ConfirmDialog(
        update_offer_title(), update_offer_detail(pending_ver_.c_str()),
        ws_if_alive(alive_, "update install", [this] {
            dialog_open_ = false;
            start_install();
        }),
        ws_if_alive(alive_, "update postpone", [this] {
            dialog_open_ = false;
            printf("[update] confirm declined (back)\n");
            set_status(update_status_postponed());
        }),
        WsAction::Update));
}

void UpdateFlow::offer_restart() {
    dialog_open_ = true;
    printf("[update] offering restart into v%s (%s)\n", pending_ver_.c_str(), origin_);
    OverlayActivity::push(new ConfirmDialog(
        update_restart_title(), update_restart_detail(),
        ws_if_alive(alive_, "update restart", [this] {
            dialog_open_ = false;
            restart_now();
        }),
        ws_if_alive(alive_, "update restart later", [this] {
            dialog_open_ = false;
            printf("[update] restart later (v%s installed)\n", pending_ver_.c_str());
            set_status(update_status_installed(pending_ver_.c_str()));
        }),
        WsAction::Confirm, update_restart_now_label(), update_restart_later_label()));
}

void UpdateFlow::start_install() {
    std::string url = updater_->asset_url();
    printf("[update] confirm -> install v%s\n", pending_ver_.c_str());
    install_started_ = true;
    updater_->start_install(url);
    last_phase_ = UpdatePhase::Downloading;  // e.g. UP_NO_SELF returns before the next tick
    last_pct_   = 0;
    set_status(update_status_downloading(0));
}

void UpdateFlow::restart_now() {
    char self_path[512] = {0};
    int rc = updater_self_path(self_path, sizeof(self_path));
    if (rc == UP_OK && envHasNextLoad()) {
        Result nrc = envSetNextLoad(self_path, nullptr);
        if (R_SUCCEEDED(nrc)) {
            printf("[update] restart now: envSetNextLoad(%s) + quit\n", self_path);
            set_status(update_status_restarting());
            brls::Application::quit();  // normal teardown; the hbloader relaunches us
            return;
        }
        printf("[update] envSetNextLoad FAILED rc=%d\n", (int)nrc);
    } else {
        printf("[update] restart unavailable (self rc=%d, hasNextLoad=%d)\n",
               rc, (int)envHasNextLoad());
    }
    set_status(update_status_restart_to_apply(pending_ver_.c_str()));
}

void UpdateFlow::tick() {
    if (!updater_) return;
    if (offer_ != Offer::None && !dialog_open_) {
        Offer o = offer_;
        offer_  = Offer::None;
        if (o == Offer::Install) offer_install();
        else offer_restart();
        return;
    }
    UpdatePhase phase = updater_->phase();

    if (phase == UpdatePhase::Downloading) {
        int pct = updater_->progress_percent();
        if (pct != last_pct_) {  // wizard sinks rebuild the screen: only on change
            last_pct_ = pct;
            set_status(update_status_downloading(pct));
        }
    }

    if (last_phase_ == UpdatePhase::Checking && (phase == UpdatePhase::Done || phase == UpdatePhase::Error)) {
        std::string ver = updater_->latest_version();
        UpdateCheckOutcome o =
            update_check_outcome(updater_->check_rc(), ver.c_str(), WS_APP_VERSION, NRO_SUFFIX);
        set_status(o.status);
        if (o.kind == UpdateCheckOutcome::Available) {
            pending_ver_ = ver;
            if (!dialog_open_) offer_ = Offer::Install;
        }
    }

    if (install_started_ && last_phase_ == UpdatePhase::Downloading &&
        (phase == UpdatePhase::Done || phase == UpdatePhase::Error)) {
        int rc = updater_->install_rc();
        if (updater_->cancelled()) {
            set_status(update_status_cancelled());
            printf("[update] install cancelled\n");
        } else if (rc == UP_OK) {
            set_status(update_status_updated(pending_ver_.c_str()));
            printf("[update] install succeeded -> restart prompt\n");
            if (!dialog_open_) offer_ = Offer::Restart;
        } else {
            set_status(updater_install_message(rc));
            printf("[update] install failed rc=%d\n", rc);
        }
        install_started_ = false;
    }

    last_phase_ = phase;
}
