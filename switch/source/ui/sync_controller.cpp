#include "sync_controller.h"
#include <cstdio>
extern "C" {
#include "waystone.h"
}
#include "sync.h"

SyncController::SyncController(WsVault* vault, AccountUid uid,
                                std::string device_id,
                                WebDavCfg dav,
                                std::vector<TitleInfo> titles)
    : vault_(vault),
      uid_(uid),
      device_id_(std::move(device_id)),
      dav_(dav),
      titles_(std::move(titles)) {}

SyncController::~SyncController() {
    join();
}

void SyncController::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    phase_.store(SyncPhase::Running);
    pushed_.store(0);
    restored_.store(0);
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = "Starting sync...";
    }
    thread_ = std::thread(&SyncController::worker, this);
}

void SyncController::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

SyncPhase SyncController::phase() const {
    return phase_.load();
}

std::string SyncController::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    return status_;
}

int SyncController::pushed_count() const {
    return pushed_.load();
}

int SyncController::restored_count() const {
    return restored_.load();
}

const std::vector<TitleInfo>& SyncController::titles() const {
    return titles_;
}

void SyncController::worker() {
    char buf[256];
    const size_t n = titles_.size();

    // Push phase
    for (size_t i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "Push %zu/%zu: %s",
                 i + 1, n, titles_[i].name.c_str());
        {
            std::lock_guard<std::mutex> lk(mu_);
            status_ = buf;
        }
        int rc = push_title(vault_, titles_[i], uid_,
                            device_id_.c_str(), dav_);
        if (rc < 0) {
            snprintf(buf, sizeof(buf), "Error pushing %s",
                     titles_[i].name.c_str());
            {
                std::lock_guard<std::mutex> lk(mu_);
                status_ = buf;
            }
            phase_.store(SyncPhase::Error);
            running_.store(false);
            return;
        }
        if (rc > 0) {
            pushed_.fetch_add(rc);
        }
    }

    // Pull phase
    for (size_t i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "Pull %zu/%zu: %s",
                 i + 1, n, titles_[i].name.c_str());
        {
            std::lock_guard<std::mutex> lk(mu_);
            status_ = buf;
        }
        int rc = pull_title(vault_, titles_[i], uid_,
                            device_id_.c_str(), dav_);
        if (rc < 0) {
            snprintf(buf, sizeof(buf), "Error pulling %s",
                     titles_[i].name.c_str());
            {
                std::lock_guard<std::mutex> lk(mu_);
                status_ = buf;
            }
            phase_.store(SyncPhase::Error);
            running_.store(false);
            return;
        }
        if (rc > 0) {
            restored_.fetch_add(rc);
        }
    }

    snprintf(buf, sizeof(buf), "Done: pushed %d / restored %d",
             pushed_.load(), restored_.load());
    {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = buf;
    }
    phase_.store(SyncPhase::Done);
    running_.store(false);
}
