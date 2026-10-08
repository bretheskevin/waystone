#include "sync_worker.h"
#include "session.h"
#include "worker_thread.h"
#include <cstdio>
#include <cstring>

extern "C" {
struct Vault;
#include "waystone.h"
}
#include "sync.h"

SyncWorker::SyncWorker(WsVault* vault, const std::string& device_id,
                       const WebDavCfg& dav, std::vector<TitleInfo> titles, const WaystoneShellConfig& config)
    : vault_(vault),
      device_id_(device_id),
      dav_url_(dav.base_url),
      dav_user_(dav.user),
      dav_pass_(dav.pass),
      config_(config),
      titles_(titles),
      phase_((int)SyncPhase::Idle),
      cur_index_(-1),
      xfer_got_(0),
      xfer_total_(0),
      running_(false),
      results_(titles.size()),
      thread_(0)
{
    dav_.base_url = dav_url_.c_str();
    dav_.user     = dav_user_.c_str();
    dav_.pass     = dav_pass_.c_str();
    LightLock_Init(&mu_);
    memset(status_buf_, 0, sizeof(status_buf_));
    memset(step_buf_, 0, sizeof(step_buf_));
}

SyncWorker::~SyncWorker() {
    join();
    zeroize_string(dav_pass_);
}

void SyncWorker::thread_entry(void* arg) {
    static_cast<SyncWorker*>(arg)->worker();
}

void SyncWorker::on_step(void* ctx, const char* label) {
    SyncWorker* self = static_cast<SyncWorker*>(ctx);
    self->xfer_got_.store(0);
    self->xfer_total_.store(0);
    LightLock_Lock(&self->mu_);
    snprintf(self->step_buf_, sizeof(self->step_buf_), "%s", label ? label : "");
    LightLock_Unlock(&self->mu_);
}

bool SyncWorker::on_bytes(size_t got, size_t total, void* ctx) {
    SyncWorker* self = static_cast<SyncWorker*>(ctx);
    self->xfer_got_.store(got);
    self->xfer_total_.store(next_xfer_total(self->xfer_total_.load(), total));
    return true;
}

void SyncWorker::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = 0;
    }
    phase_.store((int)SyncPhase::Running);
    cur_index_.store(-1);
    xfer_got_.store(0);
    xfer_total_.store(0);
    {
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Starting sync...");
        step_buf_[0] = '\0';
        results_.assign(titles_.size(), TitleResult());
        LightLock_Unlock(&mu_);
    }
    printf("[sync] worker starting for %zu title(s)\n", titles_.size());
    thread_ = start_worker_thread(thread_entry, this);
    if (!thread_) {
        printf("[sync] threadCreate failed\n");
        phase_.store((int)SyncPhase::Error);
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Thread creation failed");
        LightLock_Unlock(&mu_);
        running_.store(false);
    }
}

void SyncWorker::join() {
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = 0;
    }
}

SyncPhase SyncWorker::phase() const {
    return (SyncPhase)phase_.load();
}

std::string SyncWorker::status() {
    LightLock_Lock(&mu_);
    std::string s(status_buf_);
    LightLock_Unlock(&mu_);
    return s;
}

std::string SyncWorker::step() {
    LightLock_Lock(&mu_);
    std::string s(step_buf_);
    LightLock_Unlock(&mu_);
    return s;
}

std::vector<TitleResult> SyncWorker::results() {
    LightLock_Lock(&mu_);
    std::vector<TitleResult> copy(results_);
    LightLock_Unlock(&mu_);
    return copy;
}

int SyncWorker::current_index() const   { return cur_index_.load(); }
size_t SyncWorker::bytes_got() const    { return xfer_got_.load(); }
size_t SyncWorker::bytes_total() const  { return xfer_total_.load(); }
int SyncWorker::total_count() const     { return (int)titles_.size(); }

float SyncWorker::progress() const {
    if (phase() == SyncPhase::Done) return 1.0f;
    return combined_progress(cur_index_.load(), titles_.size(), xfer_got_.load(), xfer_total_.load());
}

const std::vector<TitleInfo>& SyncWorker::titles() const { return titles_; }

void SyncWorker::begin_title(size_t i) {
    const size_t n = titles_.size();
    xfer_got_.store(0);
    xfer_total_.store(0);
    cur_index_.store((int)i);
    LightLock_Lock(&mu_);
    snprintf(status_buf_, sizeof(status_buf_), "Syncing %zu/%zu: %s", i + 1, n, titles_[i].name.c_str());
    step_buf_[0] = '\0';
    results_[i].state = TitleState::Active;
    LightLock_Unlock(&mu_);
    printf("[sync] title %zu/%zu: %s\n", i + 1, n, titles_[i].name.c_str());
}

void SyncWorker::set_result(size_t i, TitleState state, const std::string& reason) {
    LightLock_Lock(&mu_);
    results_[i].state  = state;
    results_[i].reason = reason;
    LightLock_Unlock(&mu_);
}

void SyncWorker::worker() {
    const size_t n = titles_.size();
    SyncProgress prog;
    prog.step  = on_step;
    prog.bytes = on_bytes;
    prog.ctx   = this;
    SyncEngineCfg cfg = sync_cfg_from(config_, device_id_.c_str());
    printf("[sync] worker running: %zu title(s), decide-first single pass (policy=%d safety_backup=%d)\n",
           n, cfg.conflict_policy, (int)cfg.safety_backup);

    WebDavSession* sess = webdav_session_begin(dav_);
    if (!sess) {
        printf("[sync] webdav_session_begin failed\n");
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Network init failed");
        LightLock_Unlock(&mu_);
        phase_.store((int)SyncPhase::Error);
        running_.store(false);
        return;
    }

    int failed = 0;
    for (size_t i = 0; i < n; i++) {
        begin_title(i);
        TitleTally t = sync_title(vault_, &titles_[i], titles_[i].name.c_str(), ctr_shell_ops(),
                                  cfg, sess, &prog);
        TitleState fs = final_title_state(t);
        if (fs == TitleState::Failed) failed++;
        set_result(i, fs, fs == TitleState::Failed ? t.reason : std::string());
        printf("[sync] title %zu/%zu %s -> %s %s%s\n", i + 1, n, titles_[i].name.c_str(),
               title_state_label(fs), fs == TitleState::Failed ? t.reason.c_str() : "",
               fs == TitleState::Failed ? " -- continuing" : "");
    }

    webdav_session_end(sess);
    cur_index_.store(-1);
    xfer_got_.store(0);
    xfer_total_.store(0);
    std::string headline = format_sync_headline(results());
    LightLock_Lock(&mu_);
    snprintf(status_buf_, sizeof(status_buf_), "%s", headline.c_str());
    step_buf_[0] = '\0';
    LightLock_Unlock(&mu_);
    printf("[sync] worker done: %s (%d title(s) failed)\n", headline.c_str(), failed);
    phase_.store((int)SyncPhase::Done);
    running_.store(false);
}
