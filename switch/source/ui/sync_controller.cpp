#include "sync_controller.h"
#include "session.h"
#include "sync.h"
#include <cstdio>

SyncController::SyncController(WsVault* vault, AccountUid uid, std::string device_id,
                               WebDavCfg dav, std::vector<TitleInfo> titles,
                               const WaystoneShellConfig* config)
    : vault_(vault),
      uid_(uid),
      device_id_(std::move(device_id)),
      dav_url_(dav.base_url), dav_user_(dav.user), dav_pass_(dav.pass),
      dav_{dav_url_.c_str(), dav_user_.c_str(), dav_pass_.c_str()},
      titles_(std::move(titles)),
      config_(config),
      results_(titles_.size()) {}

SyncController::~SyncController() {
    join();
    zeroize_string(dav_pass_);
}

void SyncController::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        printf("[sync] start ignored: already running\n");
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    policy_ = config_ ? static_cast<int>(config_->conflict_policy) : 0;
    safety_backup_ = config_ ? config_->safety_backup : true;
    cur_index_.store(-1);
    xfer_got_.store(0);
    xfer_total_.store(0);
    phase_.store(SyncPhase::Running);
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = "Starting sync...";
        step_.clear();
        results_.assign(titles_.size(), TitleResult());
    }
    printf("[sync] start: %zu title(s) policy=%d safety_backup=%d\n", titles_.size(), policy_,
           static_cast<int>(safety_backup_));
    thread_ = std::thread(&SyncController::worker, this);
}

void SyncController::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

SyncPhase SyncController::phase() const { return phase_.load(); }

std::string SyncController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

std::vector<TitleResult> SyncController::results() const {
    std::lock_guard<std::mutex> lk(mu_);
    return results_;
}

const std::vector<TitleInfo>& SyncController::titles() const { return titles_; }

void SyncController::on_step(void* ctx, const char* label) {
    auto* self = static_cast<SyncController*>(ctx);
    self->xfer_got_.store(0);
    self->xfer_total_.store(0);
    std::lock_guard<std::mutex> lk(self->mu_);
    self->step_ = label ? label : "";
}

bool SyncController::on_bytes(size_t got, size_t total, void* ctx) {
    auto* self = static_cast<SyncController*>(ctx);
    self->xfer_got_.store(got);
    self->xfer_total_.store(next_xfer_total(self->xfer_total_.load(), total));
    return true;
}

std::string SyncController::step() const {
    std::lock_guard<std::mutex> lk(mu_);
    return step_;
}
int SyncController::current_index() const { return cur_index_.load(); }
size_t SyncController::bytes_got() const { return xfer_got_.load(); }
size_t SyncController::bytes_total() const { return xfer_total_.load(); }
int SyncController::total_count() const { return (int)titles_.size(); }

float SyncController::progress() const {
    if (phase() == SyncPhase::Done) return 1.0f;
    return combined_progress(cur_index_.load(), titles_.size(), xfer_got_.load(), xfer_total_.load());
}

void SyncController::set_result(size_t i, TitleState s, const std::string& reason) {
    std::lock_guard<std::mutex> lk(mu_);
    if (i >= results_.size()) return;
    results_[i].state = s;
    results_[i].reason = reason;
}

void SyncController::worker() {
    const size_t n = titles_.size();
    SyncEngineCfg cfg;
    cfg.conflict_policy = policy_;
    cfg.safety_backup = safety_backup_;
    cfg.device_id = device_id_.c_str();
    ShellOps ops = nx_shell_ops(&uid_);

    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[sync] webdav_session_begin failed\n");
        {
            std::lock_guard<std::mutex> lk(mu_);
            status_ = "Network init failed";
        }
        phase_.store(SyncPhase::Error);
        running_.store(false);
        return;
    }

    SyncProgress prog{on_step, on_bytes, this};
    printf("[sync] run: %zu title(s), decide-first single pass\n", n);
    int failed = 0;
    char buf[256];
    for (size_t i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "Syncing %zu/%zu: %s", i + 1, n, titles_[i].name.c_str());
        cur_index_.store((int)i);
        xfer_got_.store(0);
        xfer_total_.store(0);
        {
            std::lock_guard<std::mutex> lk(mu_);
            step_.clear();
            status_ = buf;
            results_[i].state = TitleState::Active;
        }
        printf("[sync] title %zu/%zu: %s\n", i + 1, n, titles_[i].name.c_str());
        TitleTally t = sync_title(vault_, &titles_[i], titles_[i].name.c_str(), ops, cfg, sess, &prog);
        TitleState fs = final_title_state(t);
        bool bad = (fs == TitleState::Failed);
        if (bad) failed++;
        set_result(i, fs, bad ? t.reason : std::string());
        printf("[sync] title %zu/%zu %s -> %s%s%s\n", i + 1, n, titles_[i].name.c_str(),
               title_state_label(fs), bad ? " " : "", bad ? t.reason.c_str() : "");
    }
    webdav_session_end(sess);
    cur_index_.store(-1);

    std::string headline = format_sync_headline(results());
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = headline;
    }
    printf("[sync] run done: %s (%d failed)\n", headline.c_str(), failed);
    phase_.store(SyncPhase::Done);
    running_.store(false);
}
