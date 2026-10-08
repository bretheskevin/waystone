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
    phase_.store(SyncPhase::Running);
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = "Starting sync...";
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

    printf("[sync] run: %zu title(s), decide-first single pass\n", n);
    int failed = 0;
    char buf[256];
    for (size_t i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "Syncing %zu/%zu: %s", i + 1, n, titles_[i].name.c_str());
        {
            std::lock_guard<std::mutex> lk(mu_);
            status_ = buf;
            results_[i].state = TitleState::Active;
        }
        printf("[sync] title %zu/%zu: %s\n", i + 1, n, titles_[i].name.c_str());
        TitleTally t = sync_title(vault_, &titles_[i], titles_[i].name.c_str(), ops, cfg, sess, nullptr);
        TitleState fs = final_title_state(t);
        bool bad = (fs == TitleState::Failed);
        if (bad) failed++;
        set_result(i, fs, bad ? t.reason : std::string());
        printf("[sync] title %zu/%zu %s -> %s%s%s\n", i + 1, n, titles_[i].name.c_str(),
               title_state_label(fs), bad ? " " : "", bad ? t.reason.c_str() : "");
    }
    webdav_session_end(sess);

    std::string headline = format_sync_headline(results());
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = headline;
    }
    printf("[sync] run done: %s (%d failed)\n", headline.c_str(), failed);
    phase_.store(SyncPhase::Done);
    running_.store(false);
}
