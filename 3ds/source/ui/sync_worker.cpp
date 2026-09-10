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
                       const WebDavCfg& dav, std::vector<TitleInfo> titles)
    : vault_(vault),
      device_id_(device_id),
      dav_url_(dav.base_url),
      dav_user_(dav.user),
      dav_pass_(dav.pass),
      titles_(titles),
      phase_((int)SyncPhase::Idle),
      pushed_(0),
      restored_(0),
      running_(false),
      thread_(0)
{
    dav_.base_url = dav_url_.c_str();
    dav_.user     = dav_user_.c_str();
    dav_.pass     = dav_pass_.c_str();
    LightLock_Init(&mu_);
    memset(status_buf_, 0, sizeof(status_buf_));
}

SyncWorker::~SyncWorker() {
    join();
    zeroize_string(dav_pass_);
}

void SyncWorker::thread_entry(void* arg) {
    static_cast<SyncWorker*>(arg)->worker();
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
    pushed_.store(0);
    restored_.store(0);
    {
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_), "Starting sync...");
        LightLock_Unlock(&mu_);
    }
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

int SyncWorker::pushed_count() const  { return pushed_.load(); }
int SyncWorker::restored_count() const { return restored_.load(); }
int SyncWorker::total_count() const    { return (int)titles_.size(); }

const std::vector<TitleInfo>& SyncWorker::titles() const { return titles_; }

void SyncWorker::worker() {
    const size_t n = titles_.size();

    // Push phase
    for (size_t i = 0; i < n; i++) {
        {
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Push %zu/%zu: %s", i + 1, n, titles_[i].name.c_str());
            LightLock_Unlock(&mu_);
        }
        int rc = push_title(vault_, titles_[i], device_id_.c_str(), dav_);
        if (rc < 0) {
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Error pushing %s", titles_[i].name.c_str());
            LightLock_Unlock(&mu_);
            phase_.store((int)SyncPhase::Error);
            running_.store(false);
            return;
        }
        if (rc > 0) pushed_.fetch_add(rc);
    }

    // Pull phase
    for (size_t i = 0; i < n; i++) {
        {
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Pull %zu/%zu: %s", i + 1, n, titles_[i].name.c_str());
            LightLock_Unlock(&mu_);
        }
        int rc = pull_title(vault_, titles_[i], device_id_.c_str(), dav_);
        if (rc < 0) {
            LightLock_Lock(&mu_);
            snprintf(status_buf_, sizeof(status_buf_),
                     "Error pulling %s", titles_[i].name.c_str());
            LightLock_Unlock(&mu_);
            phase_.store((int)SyncPhase::Error);
            running_.store(false);
            return;
        }
        if (rc > 0) restored_.fetch_add(rc);
    }

    {
        LightLock_Lock(&mu_);
        snprintf(status_buf_, sizeof(status_buf_),
                 "Done: pushed %d / restored %d", pushed_.load(), restored_.load());
        LightLock_Unlock(&mu_);
    }
    phase_.store((int)SyncPhase::Done);
    running_.store(false);
}
